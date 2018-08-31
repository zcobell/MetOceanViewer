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
#ifndef NDBC_H
#define NDBC_H

#include <QObject>
#include <QQuickWidget>
#include "chartview.h"
#include "hmdf.h"
#include "stationmodel.h"

class Ndbc : public QObject {
  Q_OBJECT
 public:
  explicit Ndbc(QQuickWidget *inMap, ChartView *inChart,
                QComboBox *inProductBox, QDateTimeEdit *inStartDateEdit,
                QDateTimeEdit *inEndDateEdit, QStatusBar *instatusBar,
                StationModel *stationModel, QString *inSelectedStation,
                QObject *parent = nullptr);

  int plotStation();
  int replotStation(int index);
  bool dataReady() const;
  int saveData(QString filename);
  int saveImage(QString filename, QString filter);

  QString getSelectedMarker();

 signals:
  void ndbcError(QString);

 private:
  int plot(int index);

  QQuickWidget *m_map;
  ChartView *m_chartView;
  QComboBox *m_productBox;
  QDateTimeEdit *m_startDateEdit, *m_endDateEdit;
  StationModel *m_stationModel;
  QStatusBar *m_statusBar;
  QString *m_selectedStation;
  Hmdf *m_data;
  bool m_dataReady;

  Station m_station;
};

#endif  // NDBC_H
