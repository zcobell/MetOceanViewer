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
