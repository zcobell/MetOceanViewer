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
#ifndef MOV_XTIDE_H
#define MOV_XTIDE_H

#include <QChartView>
#include <QObject>
#include <QPrinter>
#include <QVector>
#include <QWebEngineView>
#include <QtCharts>
#include <QtWebEngine/QtWebEngine>
#include <QtWidgets>

#include "movErrors.h"
#include "movFlags.h"
#include "movGeneric.h"
#include "movQChartView.h"

using namespace QtCharts;

class movXtide : public QObject {

  Q_OBJECT

public:
  //...Constructor
  explicit movXtide(QWebEngineView *inMap, MovQChartView *inChart,
                    QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                    QComboBox *inUnits, QStatusBar *inStatusBar,
                    QObject *parent = 0);

  //...Destructor
  ~movXtide();

  //...Public Functions
  int plotXTideStation();
  QString getLoadedXTideStation();
  QString getCurrentXTideStation();
  int getClickedXTideStation();
  int getAsyncClickedXTideStation();
  int saveXTideData(QString filename, QString format);
  int saveXTidePlot(QString filename, QString filter);
  QString getErrorString();

private slots:
  void javascriptDataReturned(QString);

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
    QDateTime date;
    double value;
  };

  //...Private Variables
  QVector<XTideStationData> currentXTideStation;
  double currentXTideLat;
  double currentXTideLon;

  QString currentStationName;
  QString units;
  QString yLabel;
  QString xLabel;
  QString plotTitle;
  QString xTideErrorString;
  QString xTideExe;
  QString xTideHarmFile;
  QChart *thisChart;

  //...Pointers to GUI elements
  QWebEngineView *map;
  MovQChartView *chart;
  QDateEdit *startDateEdit, *endDateEdit;
  QComboBox *unitSelect;
  QStatusBar *statusBar;
};

#endif // MOV_XTIDE_H
