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
#ifndef USER_TIMESERIES_H
#define USER_TIMESERIES_H

#include <QObject>
#include <QVector>
#include <QChartView>
#include <QtCharts>
#include <QtWebEngine>
#include <QWebEngineView>
#include <QPrinter>
#include <QTableWidget>
#include <QDateTime>
#include <general_functions.h>
#include <mov_qchartview.h>

class user_timeseries : public QObject
{
    Q_OBJECT
public:
    explicit user_timeseries(QTableWidget *inTable, QCheckBox *inXAxisCheck,
                             QCheckBox *inYAxisCheck, QDateEdit *inStartDate,
                             QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
                             QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle,
                             QLineEdit *inXLabelEdit, QLineEdit *inYLabelEdit,
                             QWebEngineView *inMap, mov_QChartView *inChart,
                             QObject *parent = 0);

    ~user_timeseries();

    //...Public functions
    int processData();
    int plotData();
    int getCurrentMarkerID();
    int getClickedMarkerID();
    QString getErrorString();

private slots:
    void handleLegendMarkerClicked();

private:

    //...Variable Structures
    struct IMEDS_DATA
    {
        double              latitude;
        double              longitude;
        QString             StationName;
        int                 NumSnaps;
        int                 StationIndex;
        QVector<QDateTime>  date;
        QVector<double>     data;
        bool                isNull;
    };

    struct IMEDS{
        int nstations;
        QString header1;
        QString header2;
        QString header3;
        QVector<IMEDS_DATA> station;
        bool success;
    };

    struct ADCNC{
        int nstations;
        int NumSnaps;
        QString DataType;
        QVector<double> time;
        QVector< QVector<double> > data;
        QVector<double> latitude;
        QVector<double> longitude;
        QVector<QString> station_name;
        bool success;
        int err;
    };

    struct ADCASCII{
        int nstations;
        int NumSnaps;
        int OutputTSFreq;
        int NumColumns;
        double OutputTimeFreq;
        QVector<double> time;
        QVector< QVector<double> > data;
        QVector<double> latitude;
        QVector<double> longitude;
        QVector<QString> station_name;
        bool success;
    };

    //...Private functions
    int getMarkerIDFromMap();
    int setMarkerID();
    int readADCIRCnetCDF(QString filename, ADCNC &MyData);
    int GetUniqueStationList(QVector<IMEDS> Data,QVector<double> &X,QVector<double> &Y);
    int BuildRevisedIMEDS(QVector<IMEDS> Data,QVector<double> X,QVector<double> Y,QVector<IMEDS> &DataOut);
    int readIMEDS(QString filename, IMEDS &Output);
    int readADCIRCascii(QString filename, QString stationfile, ADCASCII &MyData);
    int ADCIRC_to_IMEDS(ADCASCII ASCII, QDateTime Cold, IMEDS &MyOutput);
    int NetCDF_to_IMEDS(ADCNC netcdf, QDateTime Cold, IMEDS &Output);
    int getUniqueStationList(QVector<IMEDS> Data, QVector<double> &X, QVector<double> &Y);
    int buildRevisedIMEDS(QVector<IMEDS> Data,QVector<double> X, QVector<double> Y, QVector<IMEDS> &DataOut);
    int getDataBounds(double &ymin, double &ymax, QDateTime &minDate, QDateTime &maxDate);

    //...Private Variables
    int markerID;
    QString errorString;
    QVector<IMEDS> fileData,fileDataUnique;
    QVector<double> StationXLocs;
    QVector<double> StationYLocs;

    //...Widgets we'll need
    QTableWidget   *table;
    QCheckBox      *xAxisCheck;
    QCheckBox      *yAxisCheck;
    QDateEdit      *startDate;
    QDateEdit      *endDate;
    QDoubleSpinBox *yMinEdit;
    QDoubleSpinBox *yMaxEdit;
    QLineEdit      *plotTitle;
    QLineEdit      *xLabelEdit;
    QLineEdit      *yLabelEdit;
    QWebEngineView *map;
    mov_QChartView *chart;
    QChart         *thisChart;

};

#endif // USER_TIMESERIES_H
