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
#ifndef TIDEPREDICTION_H
#define TIDEPREDICTION_H

#include <QDateTime>
#include <QObject>
#include <QVector>

#include "hmdf.h"
#include "metocean_global.h"
#include "movStation.h"

class TidePrediction {
 public:
  explicit METOCEANSHARED_EXPORT TidePrediction(std::string root);

  METOCEANSHARED_EXPORT ~TidePrediction();

  void METOCEANSHARED_EXPORT deleteHarmonicsOnExit(bool b);

  int METOCEANSHARED_EXPORT get(MovStation &s, QDateTime startDate,
                                QDateTime endDate, int interval, Hmdf::HmdfData *data);

 private:
  void initHarmonicsDatabase();

  bool m_deleteHarmonicsOnExit = true;

  std::string m_harmonicsDatabase;
};

#endif  // TIDEPREDICTION_H
