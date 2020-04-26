#ifndef MAPVIEW_H
#define MAPVIEW_H

#include <QQuickWidget>
#include <QTimer>
#include <memory>

#include "mapfunctions.h"
#include "stationmodel.h"

class MapView : public QQuickWidget {
  Q_OBJECT
 public:
  MapView(QVector<Station> *s, MapFunctions *m, QWidget *parent);

  Station currentStation();

  MapFunctions *mapFunctions() const;

public slots:
  void refreshStations(bool filter = true, bool activeOnly = true);
  void changeMap(int index);
  void changeMapSource(MapFunctions::MapSource s);

 private slots:
  void changeMarker(QString);
  void updateStations();

 private:
  void connectMarkerChanged();
  void connectStationRefresh();
  void disconnectMarkerChanged();
  void disconnectStationRefresh();

  QVector<Station> *m_markerLocations;
  QString m_currentMarker;
  MapFunctions *m_mapFunctions;
  QTimer *m_delayTimer;
  size_t m_delayLength;
};

#endif  // MAPVIEW_H
