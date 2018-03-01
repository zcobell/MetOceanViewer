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
#ifndef MOV_USER_TIMESERIES_H
#define MOV_USER_TIMESERIES_H

#include <QChartView>
#include <QDateTime>
#include <QObject>
#include <QPrinter>
#include <QTableWidget>
#include <QVector>
#include <QWebEngineView>
#include <QtCharts>
#include <QtWebEngine/QtWebEngine>
#include "movGeneric.h"
#include "movImeds.h"
#include "movQChartView.h"
#include "movNetcdfTimeseries.h"

class MovUserTimeseries : public QObject {
  Q_OBJECT

 public:
  explicit MovUserTimeseries(QTableWidget *inTable, QCheckBox *inXAxisCheck,
                             QCheckBox *inYAxisCheck, QDateEdit *inStartDate,
                             QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
                             QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle,
                             QLineEdit *inXLabelEdit, QLineEdit *inYLabelEdit,
                             QWebEngineView *inMap, MovQChartView *inChart,
                             QStatusBar *inStatusBar,
                             QVector<QColor> inRandomColorList,
                             QObject *parent = nullptr);

  ~MovUserTimeseries();

  //...Public functions
  int processData();
  int plotData();
  int getCurrentMarkerID();
  int getClickedMarkerID();
  int getAsyncClickedMarkerID();
  int saveImage(QString filename, QString filter);
  QString getErrorString();

 private slots:
  void javascriptDataReturned(QString);

 signals:
  void timeseriesError(QString);

 private:
  //...Private functions
  int getMarkerIDFromMap();
  int setMarkerID();
  int getUniqueStationList(QVector<MovImeds *> Data, QVector<double> &X,
                           QVector<double> &Y);
  int buildRevisedIMEDS(QVector<MovImeds *> &Data, QVector<double> X,
                        QVector<double> Y, QVector<MovImeds *> &DataOut);
  int getDataBounds(double &ymin, double &ymax, QDateTime &minDate,
                    QDateTime &maxDate, QVector<double> timeAddList);
  int getMultipleMarkersFromMap();
  int getAsyncMultipleMarkersFromMap();
  int projectStations(QVector<int> epsg,
                      QVector<MovImeds *> &projectedStations);

  //...Private Variables
  int markerID;
  QString errorString;
  QVector<MovImeds *> fileData, fileDataUnique;
  QVector<double> StationXLocs;
  QVector<double> StationYLocs;
  QVector<int> selectedStations;
  QVector<QColor> randomColorList;
  QVector<int> epsg;

  //...Widgets we'll need
  QTableWidget *table;
  QCheckBox *xAxisCheck;
  QCheckBox *yAxisCheck;
  QDateEdit *startDate;
  QDateEdit *endDate;
  QDoubleSpinBox *yMinEdit;
  QDoubleSpinBox *yMaxEdit;
  QLineEdit *plotTitle;
  QLineEdit *xLabelEdit;
  QLineEdit *yLabelEdit;
  QWebEngineView *map;
  MovQChartView *chart;
  QStatusBar *statusBar;
};

#endif  // MOV_USER_TIMESERIES_H
