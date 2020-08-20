#ifndef STATIONLIST_H
#define STATIONLIST_H

#include <QVector>

#include "movStation.h"
#include "tabtypes.h"

class StationList {
 public:
  StationList();

  QVector<MovStation> *get(TabType t);

 private:
  QVector<MovStation> m_noaa;
  QVector<MovStation> m_usgs;
  QVector<MovStation> m_ndbc;
  QVector<MovStation> m_crms;
  QVector<MovStation> m_xtide;
};

#endif  // STATIONLIST_H
