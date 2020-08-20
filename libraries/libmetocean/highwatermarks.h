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
#ifndef HIGHWATERMARKS_H
#define HIGHWATERMARKS_H

#include <vector>

#include "hwmdata.h"
#include "metocean_global.h"

class HighWaterMarks {
 public:
  explicit METOCEANSHARED_EXPORT HighWaterMarks();
  explicit METOCEANSHARED_EXPORT HighWaterMarks(const std::string filename,
                                                bool regressionThroughZero);

  int METOCEANSHARED_EXPORT read();
  void METOCEANSHARED_EXPORT clear();

  bool METOCEANSHARED_EXPORT regressionThroughZero() const;
  void METOCEANSHARED_EXPORT
  setRegressionThroughZero(bool regressionThroughZero);

  std::string METOCEANSHARED_EXPORT filename() const;
  void METOCEANSHARED_EXPORT setFilename(const std::string &filename);

  HwmData METOCEANSHARED_EXPORT *hwm(size_t index);

  size_t METOCEANSHARED_EXPORT n();
  size_t METOCEANSHARED_EXPORT nValid();

  int METOCEANSHARED_EXPORT calculateStats();

  double METOCEANSHARED_EXPORT r2() const;

  double METOCEANSHARED_EXPORT standardDeviation() const;

  double METOCEANSHARED_EXPORT slope() const;

  double METOCEANSHARED_EXPORT intercept() const;

 private:
  std::vector<HwmData> m_hwms;
  std::string m_filename;
  bool m_regressionThroughZero;
  double m_r2;
  double m_standardDeviation;
  double m_slope;
  double m_intercept;
  double m_n2;
};

#endif  // HIGHWATERMARKS_H
