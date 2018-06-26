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
#ifndef TIDEPREDICTION_H
#define TIDEPREDICTION_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include "hmdf.h"
#include "metoceanviewer_global.h"
#include "station.h"

class METOCEANSHARED_EXPORT TidePrediction : public QObject {
  Q_OBJECT
 public:
  explicit TidePrediction(QString root, QObject *parent = nullptr);

  ~TidePrediction();

  void deleteHarmonicsOnExit(bool b);

  int get(Station &s, QDateTime startDate, QDateTime endDate, int interval,
          Hmdf *data);

 private:
  void initHarmonicsDatabase();

  bool m_deleteHarmonicsOnExit = true;

  QString m_harmonicsDatabase;
};

#endif  // TIDEPREDICTION_H
