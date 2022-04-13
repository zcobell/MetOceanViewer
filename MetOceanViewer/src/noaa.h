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

#ifndef NOAA_H
#define NOAA_H

#include <QChartView>
#include <QNetworkInterface>
#include <QPrinter>
#include <QQuickWidget>
#include <QUrl>
#include <QVector>
#include <QtCharts>
#include <QtNetwork>
#include "errors.h"
#include "hmdf.h"
#include "stationmodel.h"
#include "timezone.h"

//...Forward declare classes
class ChartView;

class Noaa : public QObject {
  Q_OBJECT

 public:
  //...Constructor
  explicit Noaa(QQuickWidget *inMap, ChartView *inChart,
                QDateTimeEdit *inStartDateEdit, QDateTimeEdit *inEndDateEdit,
                QComboBox *inNoaaProduct, QComboBox *inNoaaUnits,
                QComboBox *inNoaaDatum, QCheckBox *inNoaaVDatum,
                QStatusBar *inStatusBar, QComboBox *inNoaaTimezoneLocation,
                QComboBox *inNoaaTimezone, StationModel *inStationModel,
                QString *inSelectedStation, QObject *parent = nullptr);

  //...Destructor
  ~Noaa();

  //...Public Functions
  int plotNOAAStation();
  int saveNOAAImage(QString filename, QString filter);
  int saveNOAAData(QString filename);
  int getLoadedNOAAStation();
  int getClickedNOAAStation();
  QString getNOAAErrorString();
  int replotChart(Timezone *newTimezone);
  void setActiveMarker(QString marker);

 signals:
  void noaaError(QString);

 private:
  //...Private Functions
  int fetchNOAAData();
  int prepNOAAResponse();
  int getNoaaProductId(QString &product1, QString &product2);
  int getNoaaProductLabel(QString &product);
  int getNoaaProductSeriesNaming(QString &product1, QString &product2);
  QString getDatumLabel();
  QString getUnitsLabel();
  int getDataBounds(double &ymin, double &ymax);
  int generateLabels();
  int plotChart();

  //...Private Variables
  QVector<QVector<QByteArray>> m_webData;
  int m_productIndex;

  QString m_datum;
  QString m_units;
  QString m_ylabel;
  QString m_plotTitle;
  QString m_errorString;

  QDateTime m_startDate;
  QDateTime m_endDate;

  QVector<Hmdf *> m_currentStationData;

  Timezone *tz;
  int m_offsetSeconds;
  int m_priorOffsetSeconds;
  int m_loadedStationId;

  //...Pointers to GUI elements
  QQuickWidget *m_quickMap;
  ChartView *m_chartView;
  QDateTimeEdit *m_startDateEdit, *m_endDateEdit;
  QComboBox *m_comboProduct, *m_comboUnits, *m_comboDatum,
      *m_comboTimezoneLocation, *m_comboTimezone;
  QCheckBox *m_checkNoaaVdatum;
  QStatusBar *m_statusBar;
  StationModel *m_stationModel;
  Station m_station;
  QString *m_selectedStation;
};

#endif  // NOAA_H
