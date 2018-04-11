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
#ifndef USGS_H
#define USGS_H

#include <QNetworkInterface>
#include <QQuickWidget>
#include <QUrl>
#include <QVector>
#include <QtCharts>
#include <QtNetwork>
#include <QtPrintSupport>
#include <QtWebEngineWidgets>
#include "chartview.h"
#include "errors.h"
#include "generic.h"
#include "stationmodel.h"
#include "timezone.h"

using namespace QtCharts;

class Usgs : public QObject {
  Q_OBJECT

 public:
  explicit Usgs(QQuickWidget *inMap, ChartView *inChart,
                QRadioButton *inDailyButton, QRadioButton *inHistoricButton,
                QRadioButton *inInstantButton, QComboBox *inProductBox,
                QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                QStatusBar *instatusBar, QComboBox *inUSGSTimezoneLocation,
                QComboBox *inUSGSTimezone, StationModel *stationModel,
                QString *inSelectedStation, QObject *parent = nullptr);

  ~Usgs();

  //...Public functions
  bool getUSGSBeenPlotted();
  int plotNewUSGSStation();
  int replotCurrentUSGSStation(int index);
  int setUSGSBeenPlotted(bool input);
  int saveUSGSImage(QString filename, QString filter);
  int saveUSGSData(QString filename, QString format);
  QString getUSGSErrorString();
  QString getLoadedUSGSStation();
  QString getClickedUSGSStation();
  int replotChart(Timezone *newTimezone);

  static void addStationsToModel(StationModel *model);

 signals:
  void usgsError(QString);

 private:
  int getTimezoneOffset(QString timezone);
  int fetchUSGSData();
  int plotUSGS();
  int readUSGSDataFinished(QNetworkReply *);
  int formatUSGSInstantResponse(QByteArray Input);
  int formatUSGSDailyResponse(QByteArray Input);
  int getDataBounds(double &ymin, double &ymax);

  //...Data structures
  struct USGSData {
    int NumDataPoints;
    QString Description;
    QVector<QDateTime> Date;
    QVector<double> Data;
  };

  struct USGSStationData {
    QDate Date;
    QTime Time;
    double value;
  };

  //...Pointers to variables
  QQuickWidget *map;
  ChartView *chart;
  QRadioButton *dailyButton, *historicButton, *instantButton;
  QComboBox *productBox, *usgsTimezoneLocation, *usgsTimezone;
  QDateEdit *startDateEdit, *endDateEdit;
  QStatusBar *statusBar;
  // QChart *thisChart;

  //...Private variables
  bool USGSDataReady;
  bool USGSBeenPlotted;
  int USGSdataMethod;
  int offsetSeconds;
  int priorOffsetSeconds;
  int ProductIndex;
  double CurrentUSGSLat;
  double CurrentUSGSLon;
  Station m_currentStation;
  QString USGSMarkerID;
  QString CurrentUSGSStationName;
  QString USGSErrorString;
  QString ProductName;
  QString Units, Datum, yLabel;
  QDateTime requestStartDate;
  QDateTime requestEndDate;
  QVector<QString> Parameters;
  QVector<USGSStationData> USGSPlot;
  QVector<USGSData> CurrentUSGSStation;
  Timezone *tz;
  StationModel *m_stationModel;
  QString *m_selectedStation;
};

#endif  // USGS_H
