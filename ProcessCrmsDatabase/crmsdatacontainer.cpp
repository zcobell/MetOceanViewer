#include "crmsdatacontainer.h"

CrmsDataContainer::CrmsDataContainer(size_t size) {
  this->m_values = new float[size];
}

CrmsDataContainer::~CrmsDataContainer() { delete[] this->m_values; }

std::string CrmsDataContainer::id() const { return this->m_id; }

void CrmsDataContainer::setId(const std::string &id) { this->m_id = id; }

bool CrmsDataContainer::valid() const { return this->m_valid; }

void CrmsDataContainer::setValid(bool valid) { this->m_valid = valid; }

long long CrmsDataContainer::datetime() const { return this->m_datetime; }

void CrmsDataContainer::setDatetime(long long datetime) {
  this->m_datetime = datetime;
}

float CrmsDataContainer::value(size_t index) const {
  return this->m_values[index];
}

void CrmsDataContainer::setValue(size_t index, float value) {
  this->m_values[index] = value;
}
