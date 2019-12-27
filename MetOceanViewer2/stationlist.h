#ifndef STATIONLIST_H
#define STATIONLIST_H

#include <QVector>

#include "station.h"
#include "tabtypes.h"

class StationList {
 public:
  StationList();

  QVector<Station> *get(TabType t);

 private:
  QVector<Station> m_noaa;
  QVector<Station> m_usgs;
  QVector<Station> m_ndbc;
  QVector<Station> m_crms;
  QVector<Station> m_xtide;
};

#endif  // STATIONLIST_H
