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

#ifndef MOV_NOAA_H
#define MOV_NOAA_H

#include <QChartView>
#include <QNetworkInterface>
#include <QPrinter>
#include <QQuickWidget>
#include <QUrl>
#include <QVector>
#include <QWebEngineView>
#include <QtCharts>
#include <QtNetwork>
#include <QtWebEngine/QtWebEngine>
#include "movErrors.h"
#include "stationmodel.h"
#include "timezone.h"

//...Forward declare classes
class MovQChartView;
class MovImeds;

using namespace QtCharts;

class MovNoaa : public QObject {
  Q_OBJECT

 public:
  //...Constructor
  explicit MovNoaa(QQuickWidget *inMap, MovQChartView *inChart,
                   QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                   QComboBox *inNoaaProduct, QComboBox *inNoaaUnits,
                   QComboBox *inNoaaDatum, QStatusBar *inStatusBar,
                   QComboBox *inNoaaTimezoneLocation, QComboBox *inNoaaTimezone,
                   StationModel *stationModel, QString *selectedStation,
                   QObject *parent = nullptr);

  //...Destructor
  ~MovNoaa();

  //...Public Functions
  int plotNOAAStation();
  int saveNOAAImage(QString filename, QString filter);
  int saveNOAAData(QString filename, QString PreviousDirectory, QString format);
  int getLoadedNOAAStation();
  int getClickedNOAAStation();
  QString getNOAAErrorString();
  int replotChart(Timezone *newTimezone);
  void setActiveMarker(QString marker);

  static void addStationsToModel(StationModel *model);

 private slots:
  // void javascriptDataReturned(QString);

 signals:
  void noaaError(QString);

 private:
  //...Private Functions
  int formatNOAAResponse(QVector<QByteArray> Input, QString &ErrorString,
                         int index);
  void readNOAAResponse(QNetworkReply *reply, int index, int index2);
  int fetchNOAAData();
  int prepNOAAResponse();
  int retrieveProduct(int type, QString &Product, QString &Product2);
  int getDataBounds(double &ymin, double &ymax);
  int generateLabels();
  int plotChart();
  int setNOAAStation();
  int getNOAAStation(QString &NOAAStationName, double &longitude,
                     double &latitude);

  //...Private Variables
  QVector<QVector<QByteArray>> NOAAWebData;
  int NOAAMarkerID;
  int ProductIndex;

  QString Datum;
  QString Units;
  QString yLabel;
  QString plotTitle;
  QString NOAAErrorString;

  QDateTime StartDate;
  QDateTime EndDate;

  QVector<MovImeds *> CurrentNOAAStation;
  QVector<QString> ErrorString;

  Timezone *tz;
  int offsetSeconds;
  int priorOffsetSeconds;

  //...Pointers to GUI elements
  QQuickWidget *map;
  MovQChartView *chart;
  QDateEdit *startDateEdit, *endDateEdit;
  QComboBox *noaaProduct, *noaaUnits, *noaaDatum, *noaaTimezoneLocation,
      *noaaTimezone;
  QStatusBar *statusBar;
  StationModel *stationModel;
  Station m_station;
  QString *selectedStation;

  //...Keep the local chart here for reference
  QChart *thisChart;
};

#endif  // MOV_NOAA_H
