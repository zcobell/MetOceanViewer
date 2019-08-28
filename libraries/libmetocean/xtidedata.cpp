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

XtideData::XtideData(Station &station, QDateTime startDate, QDateTime endDate,
                     QString rootDriectory, QObject *parent)
    : WaterData(station, startDate, endDate, parent) {
  //...Root application directory. We'll store the harmonics file here
  this->m_rootDirectory = rootDriectory;

  //...Creates the tide prediction object
  this->m_tidePrediction = new TidePrediction(this->m_rootDirectory, this);
  this->m_tidePrediction->deleteHarmonicsOnExit(false);

  //...Default tide prediction interval in seconds, 5 minutes
  this->m_interval = 300;
}

int XtideData::retrieveData(Hmdf *data, Datum::VDatum datum) {
  Station s = this->station();
  int ierr = this->m_tidePrediction->get(s, this->startDate(), this->endDate(),
                                         this->interval(), data);
  ierr += data->applyDatumCorrection(s, datum);
  return ierr;
}

int XtideData::interval() const { return this->m_interval; }

void XtideData::setInterval(int interval) { this->m_interval = interval; }
