#ifndef MAPVIEW_H
#define MAPVIEW_H

#include <QQuickWidget>
#include <memory>

#include "mapfunctions.h"
#include "stationmodel.h"

class MapView : public QQuickWidget {
  Q_OBJECT
 public:
  MapView(QVector<Station> *s, QWidget *parent);

  Station currentStation();

 public slots:
  void refreshStations();

 private slots:
  void changeMarker(QString);

 private:
  QVector<Station> *m_markerLocations;
  MapFunctions m_mapFunctions;
  QString m_currentMarker;
};

#endif  // MAPVIEW_H
