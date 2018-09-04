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
#ifndef HWMDATA_H
#define HWMDATA_H

#include <QGeoCoordinate>
#include <QObject>

class HwmData : public QObject {
  Q_OBJECT
 public:
  explicit HwmData(QObject *parent = nullptr);
  explicit HwmData(QGeoCoordinate coordinate, double topoElevation,
                   double modeledElevation, double observedElevation,
                   QObject *parent = nullptr);

  double topoElevation() const;
  void setTopoElevation(double topoElevation);

  double modeledElevation() const;
  void setModeledElevation(double modeledElevation);

  double observedElevation() const;
  void setObservedElevation(double observedElevation);

  bool isValid();

  double modeledError();

  QGeoCoordinate *coordinate();

 private:
  double m_topoElevation;
  double m_modeledElevation;
  double m_observedElevation;
  QGeoCoordinate m_coordinate;
};

#endif  // HWMDATA_H
