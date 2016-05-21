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
#ifndef USER_TIMESERIES_H
#define USER_TIMESERIES_H

#include <QObject>
#include <QVector>
#include <QChartView>
#include <QtCharts>
#include <QtWebEngine/QtWebEngine>
#include <QWebEngineView>
#include <QPrinter>
#include <QTableWidget>
#include <QDateTime>

#include "mov_generic.h"
#include "mov_qchartview.h"
#include "imeds.h"

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
                             QStatusBar *inStatusBar, QVector<QColor> inRandomColorList,
                             QObject *parent = 0);

    ~user_timeseries();

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
    int GetUniqueStationList(QVector<imeds*> Data,QVector<double> &X,QVector<double> &Y);
    int getUniqueStationList(QVector<imeds*> Data, QVector<double> &X, QVector<double> &Y);
    int buildRevisedIMEDS(QVector<imeds *> &Data, QVector<double> X, QVector<double> Y, QVector<imeds*> &DataOut);
    int getDataBounds(double &ymin, double &ymax, QDateTime &minDate, QDateTime &maxDate, QVector<double> timeAddList);
    int getMultipleMarkersFromMap();
    int getAsyncMultipleMarkersFromMap();

    //...Private Variables
    int markerID;
    QString errorString;
    QVector<imeds*> fileData,fileDataUnique;
    QVector<double> StationXLocs;
    QVector<double> StationYLocs;
    QVector<int>    selectedStations;
    QVector<QColor> randomColorList;

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
    QStatusBar     *statusBar;

};

#endif // USER_TIMESERIES_H
