#ifndef STATIONLIST_H
#define STATIONLIST_H

#include <QVector>

#include "movStation.h"
#include "tabtypes.h"

class StationList {
 public:
  StationList();

  std::vector<MovStation> *get(TabType t);

 private:
  std::vector<MovStation> m_noaa;
  std::vector<MovStation> m_usgs;
  std::vector<MovStation> m_ndbc;
  std::vector<MovStation> m_crms;
  std::vector<MovStation> m_xtide;
};

#endif  // STATIONLIST_H
