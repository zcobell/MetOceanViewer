#include "stationlist.h"

#include "stationlocations.h"

StationList::StationList() {
  this->m_noaa = StationLocations::readMarkers(StationLocations::NOAA);
  this->m_usgs = StationLocations::readMarkers(StationLocations::USGS);
  this->m_ndbc = StationLocations::readMarkers(StationLocations::NDBC);
  // this->m_crms = StationLocations::readMarkers(StationLocations::CRMS);
  this->m_xtide = StationLocations::readMarkers(StationLocations::XTIDE);
}

QVector<Station> *StationList::get(TabType t) {
  switch (t) {
    case TabType::NOAA:
      return &this->m_noaa;
    case TabType::USGS:
      return &this->m_usgs;
    case TabType::NDBC:
      return &this->m_ndbc;
    case TabType::CRMS:
      return &this->m_crms;
    case TabType::XTIDE:
      return &this->m_xtide;
    default:
      qDebug() << "ERROR: Could not find station list";
      return nullptr;
  }
}
