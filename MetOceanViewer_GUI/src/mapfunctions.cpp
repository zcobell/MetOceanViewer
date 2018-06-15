/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#include "mapfunctions.h"
#include <QGeoRectangle>
#include <QGeoShape>
#include <QQmlContext>
#include "errors.h"
#include "mapfunctionsprivate.h"

MapFunctions::MapFunctions(QObject *parent) : QObject(parent) {
  this->m_mapSource = 0;
  this->m_defaultMapIndex = 0;
  this->m_mapboxApiKey = "";
}

QVector<Station> MapFunctions::readMarkers(
    MapFunctions::MarkerType markerType) {
  if (markerType == NOAA) {
    return MapFunctionsPrivate::readNoaaMarkers();
  } else if (markerType == USGS) {
    return MapFunctionsPrivate::readUsgsMarkers();
  } else if (markerType == XTIDE) {
    return MapFunctionsPrivate::readXtideMarkers();
  } else {
    QVector<Station> output;
    return output;
  }
}

int MapFunctions::refreshMarkers(StationModel *model, QQuickWidget *map,
                                 QVector<Station> &locations) {
  //...Clear current markers
  model->clear();

  //...Get the bounding area
  QVariant var;
  QMetaObject::invokeMethod(map->rootObject(), "getVisibleRegion",
                            Q_RETURN_ARG(QVariant, var));
  QGeoShape visibleRegion = qvariant_cast<QGeoShape>(var);
  QGeoRectangle boundingBox = visibleRegion.boundingGeoRectangle();

  //...Get coordinates
  double x1 = boundingBox.topLeft().longitude();
  double y1 = boundingBox.topLeft().latitude();
  double x2 = boundingBox.bottomRight().longitude();
  double y2 = boundingBox.bottomRight().latitude();

  //...Orient box to 0->360
  if (x1 < 0.0) x1 = x1 + 360.0;
  if (x2 < 0.0) x2 = x2 + 360.0;

  double xl = std::min(x1, x2);
  double xr = std::max(x1, x2);
  double yb = std::min(y1, y2);
  double yt = std::max(y1, y2);

  QVector<Station> visibleMarkers;

  //...Get the objects inside the viewport
  for (int i = 0; i < locations.size(); i++) {
    double x = locations.at(i).coordinate().longitude();
    double y = locations.at(i).coordinate().latitude();
    if (x < 0.0) x = x + 360.0;
    if (x <= xr && x >= xl && y <= yt && y >= yb)
      visibleMarkers.push_back(locations.at(i));
  }

  if (visibleMarkers.length() <= MAX_NUM_DISPLAYED_STATIONS) {
    model->addMarkers(visibleMarkers);
  }

  return visibleMarkers.length();
}

void MapFunctions::setMapTypes(QComboBox *comboBox) {
  QStringList esriList = QStringList() << "World Street Map"
                                       << "World Imagery"
                                       << "World Terrain Base"
                                       << "World Topography"
                                       << "USA Topo Map"
                                       << "National Geographic World Map"
                                       << "Light Gray Canvas"
                                       << "World Physical Map"
                                       << "World Shaded Relief"
                                       << "World Ocean Base"
                                       << "Dark Gray Canvas"
                                       << "DeLorme World Basemap";
  QStringList mapboxList = QStringList() << "Streets"
                                         << "Light"
                                         << "Dark"
                                         << "Satellite"
                                         << "Streets-Satellite"
                                         << "Wheatpaste"
                                         << "Streets-Basic"
                                         << "Comic"
                                         << "Outdoors"
                                         << "Run-Bike-Hike"
                                         << "Pencil"
                                         << "Pirates"
                                         << "Emerald"
                                         << "High-Contrast";
  comboBox->clear();
  if (this->m_mapSource == 0) {
    comboBox->addItems(esriList);
  } else {
    comboBox->addItems(mapboxList);
  }
  return;
}

int MapFunctions::mapSource() const { return this->m_mapSource; }

void MapFunctions::setMapSource(int mapSource) {
  this->m_mapSource = mapSource;
}

void MapFunctions::setMapQmlFile(QQuickWidget *map) {
  if (this->m_mapSource == MapSource::ESRI)
    map->setSource(QUrl("qrc:/qml/qml/EsriMapViewer.qml"));
  else if (this->m_mapSource == MapSource::MapBox) {
    map->rootContext()->setContextProperty("mapboxKey", this->m_mapboxApiKey);
    map->setSource(QUrl("qrc:/qml/qml/MapboxMapViewer.qml"));
  }
  return;
}

QString MapFunctions::mapboxApiKey() const { return this->m_mapboxApiKey; }

void MapFunctions::setMapboxApiKey(const QString &mapboxApiKey) {
  this->m_mapboxApiKey = mapboxApiKey;
  this->saveMapboxKeyToDisk();
}

void MapFunctions::getMapboxKeyFromDisk() {
  QString path = qApp->applicationDirPath();
  QFile apiKeyFile(path + "/mapbox.key");
  if (apiKeyFile.exists()) {
    apiKeyFile.open(QIODevice::ReadOnly);
    QString key = apiKeyFile.readLine();
    this->m_mapboxApiKey = key;
    apiKeyFile.close();
  }
  return;
}

void MapFunctions::saveMapboxKeyToDisk() {
  QString path = qApp->applicationDirPath();
  QFile apiKeyFile(path + QStringLiteral("/mapbox.key"));
  apiKeyFile.open(QIODevice::WriteOnly);
  apiKeyFile.write(this->m_mapboxApiKey.toStdString().c_str());
  apiKeyFile.close();
  return;
}

void MapFunctions::getDefaultMapTypeFromDisk() {
  QString path = qApp->applicationDirPath();
  QFile defaultMapFile(path + QStringLiteral("/default.map"));
  if (defaultMapFile.exists()) {
    defaultMapFile.open(QIODevice::ReadOnly);
    QString typeString = defaultMapFile.readLine().simplified();
    QString indexString = defaultMapFile.readLine().simplified();
    defaultMapFile.close();
    this->m_defaultMapIndex = indexString.toInt();
    this->m_mapSource = typeString.toInt();
  }
  return;
}

void MapFunctions::saveDefaultMapTypeToDisk() {
  QString path = qApp->applicationDirPath();
  QFile defaultMapFile(path + QStringLiteral("/default.map"));
  defaultMapFile.open(QIODevice::WriteOnly);
  defaultMapFile.write(
      QString(QString::number(this->m_mapSource) + QStringLiteral("\n"))
          .toStdString()
          .c_str());
  defaultMapFile.write(
      QString::number(this->m_defaultMapIndex).toStdString().c_str());
  defaultMapFile.close();
}

int MapFunctions::getDefaultMapIndex() const { return this->m_defaultMapIndex; }

void MapFunctions::setDefaultMapIndex(int defaultMapIndex) {
  this->m_defaultMapIndex = defaultMapIndex;
}
