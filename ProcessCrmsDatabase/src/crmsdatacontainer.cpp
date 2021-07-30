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
#include "crmsdatacontainer.h"

CrmsDataContainer::CrmsDataContainer(size_t size)
    : m_id(std::string()),
      m_valid(false),
      m_size(size),
      m_datetime(0),
      m_values(size, 0) {}

CrmsDataContainer::CrmsDataContainer(const CrmsDataContainer &c) {
  this->m_size = c.size();
  this->m_values.resize(m_size);
  this->m_id = c.id();
  this->m_valid = c.valid();
  this->m_datetime = c.datetime();
  for (size_t i = 0; i < this->size(); ++i) {
    this->m_values[i] = c.value(i);
  }
}

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

size_t CrmsDataContainer::size() const { return this->m_size; }
