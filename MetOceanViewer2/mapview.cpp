#include "mapview.h"

#include <QQmlContext>
#include <QTimer>

#include "stationlocations.h"

MapView::MapView(QVector<Station> *s, QWidget *parent) : QQuickWidget(parent) {
  this->m_currentMarker = QString();
  this->m_markerLocations = s;
  this->m_mapFunctions.getConfigurationFromDisk();
  this->rootContext()->setContextProperty(
      "stationModel", this->m_mapFunctions.getStationModel());
  this->rootContext()->setContextProperty(
      "markerMode", MapFunctions::MapViewerMarkerModes::SingleSelectWithDates);
  this->m_mapFunctions.setMapType(this->m_mapFunctions.getDefaultMapIndex(),
                                  this);
  this->m_mapFunctions.setMapQmlFile(this);

  //...Set up a delay timer for refreshing the stations
  this->m_delayLength = 150;
  this->m_delayTimer = new QTimer(this);
  this->m_delayTimer->setInterval(this->m_delayLength);
  connect(this->m_delayTimer, SIGNAL(timeout()), this, SLOT(refreshStations()));

  QObject *mapItem = this->rootObject();
  QObject::connect(mapItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeMarker(QString)));
  QObject::connect(mapItem, SIGNAL(mapViewChanged()), this,
                   SLOT(updateStations()));
  QMetaObject::invokeMethod(mapItem, "setMapLocation", Q_ARG(QVariant, -124.66),
                            Q_ARG(QVariant, 36.88), Q_ARG(QVariant, 1.69));
}

Station MapView::currentStation() {
  return this->m_mapFunctions.getStationModel()->findStation(
      this->m_currentMarker);
}

MapFunctions *MapView::mapFunctions() { return &this->m_mapFunctions; }

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
  int n = this->m_mapFunctions.refreshMarkers(this, this->m_markerLocations,
                                              filter, activeOnly);
  // this->stationDisplayWarning(n);
}

void MapView::changeMap(int index) {
  this->mapFunctions()->setMapType(index, this);
}
