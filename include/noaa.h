//-------------------------------GPL-------------------------------------//
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
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//

#ifndef NOAA_H
#define NOAA_H

#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QChartView>
#include <QtCharts>
#include <QtWebEngine/QtWebEngine>
#include <QWebEngineView>
#include <QPrinter>

#include "mov_errors.h"
#include "mov_flags.h"
#include "general_functions.h"
#include "mov_qchartview.h"

using namespace QtCharts;

class noaa : public QObject
{

    Q_OBJECT

public:
    //...Constructor
    explicit noaa(QWebEngineView *inMap, mov_QChartView *inChart,
                  QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                  QComboBox *inNoaaProduct, QComboBox *inNoaaUnits,
                  QComboBox *inNoaaDatum, QStatusBar *inStatusBar,
                  QObject *parent = 0);

    //...Destructor
    ~noaa();

    //...Public Functions
    int plotNOAAStation();
    int saveNOAAImage(QString filename, QString filter);
    int saveNOAAData(QString filename, QString PreviousDirectory, QString format);
    int getLoadedNOAAStation();
    int getClickedNOAAStation();
    QString getNOAAErrorString();

private slots:
    void handleLegendMarkerClicked();

private:

    //...Private Functions
    QString formatNOAAResponse(QVector<QByteArray> Input, QString &ErrorString, int index);
    void readNOAAResponse(QNetworkReply *reply, int index, int index2);
    int fetchNOAAData();
    int prepNOAAResponse();
    int retrieveProduct(int type, QString &Product, QString &Product2);
    int getDataBounds(double &ymin, double &ymax);
    int generateLabels();
    int plotChart();
    int setNOAAStation();
    int getNOAAStation(QString &NOAAStationName, double &longitude, double &latitude);

    //...Structures
    struct NOAAStationData
    {
        QDate Date;
        QTime Time;
        double value;
    };

    //...Private Variables
    QVector< QVector<QByteArray> > NOAAWebData;
    int NOAAMarkerID;
    int ProductIndex;

    double CurrentNOAALat;
    double CurrentNOAALon;

    QString CurrentNOAAStationName;
    QString Datum;
    QString Units;
    QString yLabel;
    QString plotTitle;
    QString NOAAErrorString;

    QDateTime StartDate;
    QDateTime EndDate;

    QVector< QVector<NOAAStationData> > CurrentNOAAStation;
    QVector<QString> ErrorString;

    //...Pointers to GUI elements
    QWebEngineView *map;
    mov_QChartView *chart;
    QDateEdit      *startDateEdit,*endDateEdit;
    QComboBox      *noaaProduct,*noaaUnits,*noaaDatum;
    QStatusBar     *statusBar;

    //...Keep the local chart here for reference
    QChart         *thisChart;

};

#endif // NOAA_H
