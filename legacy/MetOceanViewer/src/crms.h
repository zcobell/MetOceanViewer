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
#ifndef CRMS_H
#define CRMS_H

#include <QObject>
#include <QPrinter>
#include <QQuickWidget>
#include <QVector>
#include <QtCharts>
#include <QtWidgets>
#include "chartview.h"
#include "errors.h"
#include "generic.h"
#include "hmdf.h"
#include "stationmodel.h"

class Crms : public QObject {
  Q_OBJECT
 public:
  explicit Crms(QQuickWidget *inMap, ChartView *inChart,
                QDateTimeEdit *inStartDateEdit, QDateTimeEdit *inEndDateEdit,
                QComboBox *inProduct, QStatusBar *inStatusBar,
                StationModel *inStationModel, QString *currentStation,
                QVector<QString> &header, QMap<QString, size_t> &mapping,
                QObject *parent = nullptr);
  ~Crms();

  //...Public Functions
  int plotStation();
  int replot(size_t index);
  QString getLoadedStation();
  QString getCurrentStation();
  int saveData(QString filename, QString format);
  int savePlot(QString filename, QString filter);
  QString getErrorString();
  ChartView *chartview();

 signals:
  void error(QString);

 private:
  int getData();
  int plot(size_t index);

  bool m_working;
  QString m_errorString;
  QVector<QString> m_header;
  QMap<QString, size_t> m_map;

  //...Pointers to GUI elements
  QQuickWidget *m_quickMap;
  ChartView *m_chartView;
  QDateTimeEdit *m_startDateEdit, *m_endDateEdit;
  QComboBox *m_comboProduct;
  QStatusBar *m_statusBar;
  StationModel *m_stationModel;
  Station m_station;
  QString *m_currentStation;
  Hmdf *m_data;
};

#endif  // CRMS_H
