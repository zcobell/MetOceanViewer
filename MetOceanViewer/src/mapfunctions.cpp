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
#include <QDir>
#include <QGeoRectangle>
#include <QGeoShape>
#include <QQmlContext>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include "errors.h"
#include "generic.h"

static QStringList esriList = QStringList() << "World Street Map"
                                            << "World Imagery"
                                            << "World Terrain Base"
                                            << "World Topography"
                                            << "USA Topo Maps"
                                            << "National Geographic World Map"
                                            << "Light Gray Canvas"
                                            << "World Physical Map"
                                            << "World Shaded Relief"
                                            << "World Ocean Base"
                                            << "Dark Gray Canvas"
                                            << "DeLorme World Basemap";

#if defined(_MSC_VER) || defined(__APPLE__)
static QStringList mapboxList = QStringList() << "Streets"
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
#else
static QStringList mapboxList = QStringList() << "Streets"
                                              << "Basic"
                                              << "Bright"
                                              << "Outdoors"
                                              << "Satellite"
                                              << "Satellite-Streets"
                                              << "Light"
                                              << "Dark"
                                              << "Navigation Preview Day"
                                              << "Navigation Preview Night"
                                              << "Navigation Day"
                                              << "Navigation Night";
#endif

static QStringList osmList = QStringList() << "Street Map";
//                                           << "Satellite Map"
//                                           << "Cycle Map"
//                                           << "Transit Map"
//                                           << "Night Transit Map"
//                                           << "Terrain Map"
//                                           << "Hiking Map";

MapFunctions::MapFunctions(QObject *parent) : QObject(parent) {
  this->m_mapSource = 0;
  this->m_defaultMapIndex = 0;
  this->m_mapboxApiKey = "";
  this->m_configDirectory = Generic::configDirectory();
}

template <typename T>
bool isBetween(T start, T end, T rangeStart, T rangeEnd) {
  return (start <= rangeEnd && end >= rangeStart);
}

int MapFunctions::refreshMarkers(StationModel *model, QQuickWidget *map,
                                 QVector<Station> &locations, QDateTime &start,
                                 QDateTime &end) {
  model->clear();
  QVector<Station> visibleMarkers;
  for (size_t i = 0; i < locations.size(); ++i) {
    if (isBetween<QDateTime>(locations[i].startValidDate(),
                             locations[i].endValidDate(), start, end)) {
      visibleMarkers.push_back(locations[i]);
    }
  }
  model->addMarkers(visibleMarkers);
  return visibleMarkers.length();
}

int MapFunctions::refreshMarkers(StationModel *model, QQuickWidget *map,
                                 QVector<Station> &locations, bool filter,
                                 bool activeOnly) {
  //...Clear current markers
  model->clear();

  if (filter) {
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

      if (x <= xr && x >= xl && y <= yt && y >= yb) {
        if (activeOnly) {
          if (locations.at(i).active()) {
            visibleMarkers.push_back(locations.at(i));
          }
        } else
          visibleMarkers.push_back(locations.at(i));
      }
    }

    if (visibleMarkers.length() <= MAX_NUM_DISPLAYED_STATIONS) {
      model->addMarkers(visibleMarkers);
    }
    return visibleMarkers.length();
  } else {
    model->addMarkers(locations);
    return locations.length();
  }
}

void MapFunctions::setMapTypes(QQuickWidget *map, QComboBox *comboBox) {
  comboBox->clear();
  if (this->m_mapSource == ESRI) {
    comboBox->addItems(esriList);
  } else if (this->m_mapSource == MapBox) {
    comboBox->addItems(mapboxList);
  } else if (this->m_mapSource == OSM) {
    comboBox->addItems(osmList);
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

//...Only g++ can use MapboxGL (as of this writing). This is toggled
//   automatically here
#if defined(_MSC_VER) || defined(__APPLE__)
    map->setSource(QUrl("qrc:/qml/qml/MapboxMapViewer.qml"));
#else
    map->setSource(QUrl("qrc:/qml/qml/MapboxGLMapViewer.qml"));
#endif

  } else if (this->m_mapSource == MapSource::OSM) {
    map->setSource(QUrl("qrc:/qml/qml/OsmMapViewer.qml"));
  }
  return;
}

QString MapFunctions::mapboxApiKey() const { return this->m_mapboxApiKey; }

void MapFunctions::setMapboxApiKey(const QString &mapboxApiKey) {
  this->m_mapboxApiKey = mapboxApiKey;
  this->saveMapboxKeyToDisk();
}

void MapFunctions::getMapboxKeyFromDisk() {
  QFile apiKeyFile(this->m_configDirectory + "/mapbox.xml");
  if (apiKeyFile.exists()) {
    apiKeyFile.open(QIODevice::ReadOnly);
    QXmlStreamReader xmlReader(&apiKeyFile);

    while (!xmlReader.atEnd() && !xmlReader.hasError()) {
      QXmlStreamReader::TokenType token = xmlReader.readNext();
      if (token == QXmlStreamReader::StartDocument) continue;
      if (token == QXmlStreamReader::StartElement) {
        if (xmlReader.name() == "ApiKey") {
          this->m_mapboxApiKey = xmlReader.readElementText();
        }
      }
    }
    apiKeyFile.close();
  }
  return;
}

void MapFunctions::saveMapboxKeyToDisk() {
  QFile apiKeyFile(this->m_configDirectory + QStringLiteral("/mapbox.xml"));
  apiKeyFile.open(QIODevice::WriteOnly);
  QXmlStreamWriter xmlWriter(&apiKeyFile);
  xmlWriter.setAutoFormatting(true);
  xmlWriter.setAutoFormattingIndent(2);
  xmlWriter.writeStartDocument();
  xmlWriter.writeStartElement("MetOceanViewer");
  xmlWriter.writeStartElement("Mapbox");
  xmlWriter.writeTextElement("ApiKey", this->m_mapboxApiKey);
  xmlWriter.writeEndElement();
  xmlWriter.writeEndElement();
  xmlWriter.writeEndDocument();
  apiKeyFile.close();
  return;
}

void MapFunctions::getConfigurationFromDisk() {
  QFile defaultMapFile(this->m_configDirectory +
                       QStringLiteral("/defaultmap.xml"));
  if (defaultMapFile.exists()) {
    defaultMapFile.open(QIODevice::ReadOnly | QFile::Text);
    QXmlStreamReader xmlReader(&defaultMapFile);

    while (!xmlReader.atEnd() && !xmlReader.hasError()) {
      QXmlStreamReader::TokenType token = xmlReader.readNext();
      if (token == QXmlStreamReader::StartDocument) continue;
      if (token == QXmlStreamReader::StartElement) {
        if (xmlReader.name() == "MapSource") {
          this->m_mapSource = xmlReader.readElementText().toInt();
        } else if (xmlReader.name() == "MapIndex") {
          this->m_defaultMapIndex = xmlReader.readElementText().toInt();
        }
      }
    }
    defaultMapFile.close();
  }
  return;
}

void MapFunctions::saveConfigurationToDisk() {
  QFile defaultMapFile(this->m_configDirectory +
                       QStringLiteral("/defaultmap.xml"));
  defaultMapFile.open(QIODevice::WriteOnly);
  QXmlStreamWriter xmlWriter(&defaultMapFile);
  xmlWriter.setAutoFormatting(true);
  xmlWriter.setAutoFormattingIndent(2);
  xmlWriter.writeStartDocument();
  xmlWriter.writeStartElement("MetOceanViewer");
  xmlWriter.writeStartElement("MapConfiguration");
  xmlWriter.writeTextElement("MapSource", QString::number(this->m_mapSource));
  xmlWriter.writeTextElement("MapIndex",
                             QString::number(this->m_defaultMapIndex));
  xmlWriter.writeEndElement();
  xmlWriter.writeEndElement();
  xmlWriter.writeEndDocument();
  defaultMapFile.close();
}

int MapFunctions::getDefaultMapIndex() const { return this->m_defaultMapIndex; }

void MapFunctions::setDefaultMapIndex(int defaultMapIndex) {
  this->m_defaultMapIndex = defaultMapIndex;
}

void MapFunctions::setMapType(int index, QQuickWidget *map) {
  if (this->mapSource() == MapSource::ESRI) {
    if (index < 0 || index >= esriList.length()) return;
  } else if (this->mapSource() == MapSource::MapBox) {
    if (index < 0 || index >= mapboxList.length()) return;
  }

  map->rootContext()->setContextProperty("mapType", index);

  return;
}
