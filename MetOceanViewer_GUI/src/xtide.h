/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
#ifndef XTIDE_H
#define XTIDE_H

#include <QObject>
#include <QPrinter>
#include <QQuickWidget>
#include <QVector>
#include <QtCharts>
#include <QtWidgets>
#include "chartview.h"
#include "errors.h"
#include "generic.h"
#include "stationmodel.h"

using namespace QtCharts;

class XTide : public QObject {
  Q_OBJECT

 public:
  //...Constructor
  explicit XTide(QQuickWidget *inMap, ChartView *inChart,
                 QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                 QComboBox *inUnits, QStatusBar *inStatusBar,
                 StationModel *inStationModel, QString *currentStation,
                 QObject *parent = nullptr);

  //...Destructor
  ~XTide();

  //...Public Functions
  int plotXTideStation();
  QString getLoadedXTideStation();
  QString getCurrentXTideStation();
  int saveXTideData(QString filename, QString format);
  int saveXTidePlot(QString filename, QString filter);
  QString getErrorString();

  static void addStationsToModel(StationModel *model);

 signals:
  void xTideError(QString);

 private:
  int findXTideExe();
  int calculateXTides();
  int plotChart();
  int getDataBounds(double &min, double &max);
  int parseXTideResponse(QString xTideResponse);

  // Structures
  struct XTideStationData {
    long long date;
    double value;
  };

  //...Private Variables
  QVector<XTideStationData> currentXTideStation;

  QString units;
  QString yLabel;
  QString xLabel;
  QString plotTitle;
  QString xTideErrorString;
  QString xTideExe;
  QString xTideHarmFile;
  QChart *thisChart;

  //...Pointers to GUI elements
  QQuickWidget *map;
  ChartView *chart;
  QDateEdit *startDateEdit, *endDateEdit;
  QComboBox *unitSelect;
  QStatusBar *statusBar;
  StationModel *m_stationModel;
  Station m_station;
  QString *m_currentStation;
};

#endif  // XTIDE_H
