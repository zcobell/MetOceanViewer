#include "mapview.h"

#include <QGeoRectangle>
#include <QGeoShape>
#include <QQmlContext>
#include <QTimer>

#include "stationlocations.h"

MapView::MapView(QVector<Station> *s, MapFunctions *m, QWidget *parent)
    : QQuickWidget(parent) {
  this->m_currentMarker = QString();
  this->m_markerLocations = s;
  this->m_mapFunctions = m;
  this->m_mapFunctions->getConfigurationFromDisk();
  this->rootContext()->setContextProperty(
      "stationModel", this->m_mapFunctions->getStationModel());
  this->rootContext()->setContextProperty(
      "markerMode", MapFunctions::MapViewerMarkerModes::SingleSelectWithDates);
  this->m_mapFunctions->setMapType(this->m_mapFunctions->getDefaultMapIndex(),
                                   this);
  this->m_mapFunctions->setMapQmlFile(this);

  //...Set up a delay timer for refreshing the stations
  this->m_delayLength = 150;
  this->m_delayTimer = new QTimer(this);
  this->m_delayTimer->setInterval(this->m_delayLength);
  connect(this->m_delayTimer, SIGNAL(timeout()), this, SLOT(refreshStations()));
  this->connectMarkerChanged();
  this->connectStationRefresh();
  QMetaObject::invokeMethod(this->rootObject(), "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));
}

void MapView::connectMarkerChanged() {
  QObject::connect(this->rootObject(), SIGNAL(markerChanged(QString)), this,
                   SLOT(changeMarker(QString)));
}

void MapView::connectStationRefresh() {
  QObject::connect(this->rootObject(), SIGNAL(mapViewChanged()), this,
                   SLOT(updateStations()));
}

void MapView::disconnectMarkerChanged() {
  QObject::disconnect(this->rootObject(), SIGNAL(markerChanged(QString)), this,
                      SLOT(changeMarker(QString)));
}

void MapView::disconnectStationRefresh() {
  QObject::disconnect(this->rootObject(), SIGNAL(mapViewChanged()), this,
                      SLOT(updateStations()));
}

MapFunctions *MapView::mapFunctions() const { return m_mapFunctions; }

Station MapView::currentStation() {
  return this->m_mapFunctions->getStationModel()->findStation(
      this->m_currentMarker);
}

void MapView::changeMarker(QString markerString) {
  this->m_currentMarker = markerString;
}

void MapView::updateStations() {
  if (this->m_delayTimer->isActive()) {
    this->m_delayTimer->stop();
  }
  this->m_delayTimer->start(this->m_delayLength);
}

void MapView::refreshStations(bool filter, bool activeOnly) {
  this->m_delayTimer->stop();
  int n = this->m_mapFunctions->refreshMarkers(this, this->m_markerLocations,
                                               filter, activeOnly);
  // this->stationDisplayWarning(n);
}

void MapView::changeMap(int index) {
  this->m_mapFunctions->setMapType(index, this);
}

void MapView::changeMapSource(MapFunctions::MapSource s) {
  this->disconnectStationRefresh();
  this->disconnectMarkerChanged();

  QVariant var;
  QMetaObject::invokeMethod(this->rootObject(), "getVisibleRegion",
                            Q_RETURN_ARG(QVariant, var));
  QGeoShape visibleRegion = qvariant_cast<QGeoShape>(var);
  QGeoCoordinate center = visibleRegion.boundingGeoRectangle().center();
  QMetaObject::invokeMethod(this->rootObject(), "getCurrentZoomLevel",
                            Q_RETURN_ARG(QVariant, var));
  double zoomLevel = qvariant_cast<double>(var);

  MapFunctions m;
  m.setMapSource(s);
  m.setMapQmlFile(this);
  QMetaObject::invokeMethod(
      this->rootObject(), "setMapLocation", Q_ARG(QVariant, center.longitude()),
      Q_ARG(QVariant, center.latitude()), Q_ARG(QVariant, zoomLevel));

  this->connectMarkerChanged();
  this->connectStationRefresh();
}
