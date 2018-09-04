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
#ifndef HIGHWATERMARKS_H
#define HIGHWATERMARKS_H

#include <QObject>
#include <QVector>
#include "hwmdata.h"

class HighWaterMarks : public QObject {
  Q_OBJECT
 public:
  explicit HighWaterMarks(QObject *parent = nullptr);
  explicit HighWaterMarks(QString filename, bool regressionThroughZero,
                          QObject *parent = nullptr);

  int read();
  void clear();

  bool regressionThroughZero() const;
  void setRegressionThroughZero(bool regressionThroughZero);

  QString filename() const;
  void setFilename(const QString &filename);

  HwmData *hwm(size_t index);

  size_t n();
  size_t nValid();

  int calculateStats();

  double r2() const;

  double standardDeviation() const;

  double slope() const;

  double intercept() const;

private:
  QVector<HwmData *> m_hwms;
  QString m_filename;
  bool m_regressionThroughZero;
  double m_r2;
  double m_standardDeviation;
  double m_slope;
  double m_intercept;
  double m_n2;
};

#endif  // HIGHWATERMARKS_H
