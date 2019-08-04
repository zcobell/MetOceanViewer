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
#ifndef USGS_H
#define USGS_H

#include <QNetworkInterface>
#include <QQuickWidget>
#include <QUrl>
#include <QVector>
#include <QtCharts>
#include <QtNetwork>
#include <QtPrintSupport>
#include "chartview.h"
#include "errors.h"
#include "generic.h"
#include "stationmodel.h"
#include "usgswaterdata.h"
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
  int saveUSGSData(QString filename);
  QString getUSGSErrorString();
  QString getLoadedUSGSStation();
  QString getClickedUSGSStation();
  int replotChart(Timezone *newTimezone);

 signals:
  void usgsError(QString);

 private:
  int getTimezoneOffset(QString timezone);
  int plotUSGS();

  //...Pointers to variables
  QQuickWidget *m_quickMap;
  ChartView *m_chartView;
  QRadioButton *m_buttonDaily, *m_buttonHistoric, *m_buttonInstant;
  QComboBox *m_comboProduct, *m_comboTimezoneLocation, *m_comboTimezone;
  QDateEdit *m_startDateEdit, *m_endDateEdit;
  QStatusBar *m_statusBar;

  //...Private variables
  bool m_usgsDataReady;
  bool m_usgsBeenPlotted;
  int m_usgsDataMethod;
  int m_offsetSeconds;
  int m_priorOffsetSeconds;
  int m_productIndex;
  Station m_currentStation;
  QString m_errorString;
  QString m_productName;
  QString m_units, m_datum, m_ylabel;
  QDateTime m_requestStartDate;
  QDateTime m_requestEndDate;
  QVector<QString> m_availableDatatypes;
  Hmdf *m_allStationData;
  Timezone *m_tz;
  StationModel *m_stationModel;
  QString *m_selectedStation;
};

#endif  // USGS_H
