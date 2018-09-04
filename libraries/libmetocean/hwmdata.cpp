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
#include "hwmdata.h"

HwmData::HwmData(QObject *parent) : QObject(parent) {
  this->m_coordinate = QGeoCoordinate();
  this->m_modeledElevation = -9999;
  this->m_observedElevation = -9999;
  this->m_topoElevation = -9999;
}

HwmData::HwmData(QGeoCoordinate coordinate, double topoElevation,
                 double modeledElevation, double observedElevation,
                 QObject *parent)
    : QObject(parent) {
  this->m_coordinate = coordinate;
  this->m_topoElevation = topoElevation;
  this->m_modeledElevation = modeledElevation;
  this->m_observedElevation = observedElevation;
}

double HwmData::topoElevation() const { return m_topoElevation; }

void HwmData::setTopoElevation(double topoElevation) {
  m_topoElevation = topoElevation;
}

double HwmData::modeledElevation() const { return m_modeledElevation; }

void HwmData::setModeledElevation(double modeledElevation) {
  m_modeledElevation = modeledElevation;
}

double HwmData::observedElevation() const { return m_observedElevation; }

void HwmData::setObservedElevation(double observedElevation) {
  m_observedElevation = observedElevation;
}

bool HwmData::isValid() {
  if (this->modeledElevation() > this->topoElevation() &&
      this->modeledElevation() > -900) {
    return true;
  } else {
    return false;
  }
}

double HwmData::modeledError() {
  return this->modeledElevation() - this->observedElevation();
}

QGeoCoordinate *HwmData::coordinate() { return &this->m_coordinate; }
