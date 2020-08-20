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
#ifndef HWMDATA_H
#define HWMDATA_H

#include <QGeoCoordinate>

#include "metocean_global.h"

class HwmData {
 public:
  explicit METOCEANSHARED_EXPORT HwmData();
  explicit METOCEANSHARED_EXPORT HwmData(QGeoCoordinate coordinate,
                                         double topoElevation,
                                         double modeledElevation,
                                         double observedElevation);

  double METOCEANSHARED_EXPORT topoElevation() const;
  void METOCEANSHARED_EXPORT setTopoElevation(double topoElevation);

  double METOCEANSHARED_EXPORT modeledElevation() const;
  void METOCEANSHARED_EXPORT setModeledElevation(double modeledElevation);

  double METOCEANSHARED_EXPORT observedElevation() const;
  void METOCEANSHARED_EXPORT setObservedElevation(double observedElevation);

  bool METOCEANSHARED_EXPORT isValid();

  double METOCEANSHARED_EXPORT modeledError();

  QGeoCoordinate METOCEANSHARED_EXPORT *coordinate();

 private:
  double m_topoElevation;
  double m_modeledElevation;
  double m_observedElevation;
  QGeoCoordinate m_coordinate;
};

#endif  // HWMDATA_H
