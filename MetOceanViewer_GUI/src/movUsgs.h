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
#ifndef MOV_USGS_H
#define MOV_USGS_H


#include <QtPrintSupport>
#include <QtWebEngineWidgets>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QtCharts>

#include "movGeneric.h"
#include "movQChartView.h"
#include "movErrors.h"
#include "movFlags.h"

using namespace QtCharts;

class MovUsgs : public QObject
{

    Q_OBJECT

public:
    explicit MovUsgs(QWebEngineView *inMap,
               MovQChartView *inChart,QRadioButton *inDailyButton,
               QRadioButton *inHistoricButton,QRadioButton *inInstantButton,
               QComboBox *inProductBox, QDateEdit *inStartDateEdit,
               QDateEdit *inEndDateEdit, QStatusBar *instatusBar,QObject *parent = 0);

    ~MovUsgs();

    //...Public functions
    bool getUSGSBeenPlotted();
    int plotNewUSGSStation();
    int replotCurrentUSGSStation(int index);
    int setUSGSBeenPlotted(bool input);
    int saveUSGSImage(QString filename, QString filter);
    int saveUSGSData(QString filename,QString format);
    QString getUSGSErrorString();
    QString getLoadedUSGSStation();
    QString getClickedUSGSStation();

private slots:
    void javascriptDataReturned(QString data);


signals:
    void usgsError(QString);

private:
    //...Private functions
    QString getMarkerSelection(QString &name, double &longitude, double &latitude);
    int setMarkerSelection();
    void setAsyncMarkerSelection();
    int getTimezoneOffset(QString timezone);
    int fetchUSGSData();
    int plotUSGS();
    int readUSGSDataFinished(QNetworkReply*);
    int formatUSGSInstantResponse(QByteArray Input);
    int formatUSGSDailyResponse(QByteArray Input);
    int getDataBounds(double &ymin, double &ymax);

    //...Data structures
    struct USGSData
    {
        int NumDataPoints;
        QString Description;
        QVector<QDateTime> Date;
        QVector<double> Data;
    };

    struct USGSStationData
    {
        QDate Date;
        QTime Time;
        double value;
    };

    //...Pointers to variables
    QWebEngineView *map;
    MovQChartView *chart;
    QRadioButton   *dailyButton,*historicButton,*instantButton;
    QComboBox      *productBox;
    QDateEdit      *startDateEdit,*endDateEdit;
    QStatusBar     *statusBar;
    QChart         *thisChart;

    //...Private variables
    bool USGSDataReady;
    bool USGSBeenPlotted;
    int USGSdataMethod;
    int ProductIndex;
    double CurrentUSGSLat;
    double CurrentUSGSLon;
    QString USGSMarkerID;
    QString CurrentUSGSStationName;
    QString USGSErrorString;
    QString ProductName;
    QString Units,Datum,yLabel;
    QDateTime requestStartDate;
    QDateTime requestEndDate;
    QVector<QString> Parameters;
    QVector<USGSStationData> USGSPlot;
    QVector<USGSData> CurrentUSGSStation;

};

#endif // MOV_USGS_H
