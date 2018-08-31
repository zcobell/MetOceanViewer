/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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
#ifndef XTIDEDATA_H
#define XTIDEDATA_H

#include "metoceanviewer_global.h"
#include "tideprediction.h"
#include "waterdata.h"

class XtideData : public WaterData {
  Q_OBJECT
 public:
  XtideData(Station &station, QDateTime startDate, QDateTime endDate,
            QString rootDriectory, QObject *parent);

  int interval() const;
  void setInterval(int interval);

 private:
  int retrieveData(Hmdf *data);

  int m_interval;
  QString m_rootDirectory;
  TidePrediction *m_tidePrediction;
};

#endif  // XTIDEDATA_H
