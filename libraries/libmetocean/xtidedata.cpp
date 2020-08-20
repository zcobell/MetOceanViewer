/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#include "xtidedata.h"

XtideData::XtideData(MovStation &station, QDateTime startDate,
                     QDateTime endDate, QString rootDriectory)
    : WaterData(station, startDate, endDate),
      m_interval(300),
      m_rootDirectory(rootDriectory),
      m_tidePrediction(new TidePrediction(rootDriectory)) {
  this->m_tidePrediction->deleteHarmonicsOnExit(false);
}

int XtideData::retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum) {
  MovStation s = this->station();
  int ierr = this->m_tidePrediction->get(s, this->startDate(), this->endDate(),
                                         this->interval(), data);
  data->station(0)->shift(0, s.offset(datum));
  return ierr;
}

int XtideData::interval() const { return this->m_interval; }

void XtideData::setInterval(int interval) { this->m_interval = interval; }
