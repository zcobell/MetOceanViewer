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
