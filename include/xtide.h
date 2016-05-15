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
#ifndef XTIDE_H
#define XTIDE_H

#include <QObject>
#include <QtWidgets>
#include <QWebEngineView>
#include <QtCharts>
#include <QChartView>
#include <QPrinter>
#include <QVector>
#include <QtWebEngine/QtWebEngine>

#include "mov_qchartview.h"
#include "mov_errors.h"
#include "mov_flags.h"
#include "general_functions.h"

using namespace QtCharts;

class XTide : public QObject
{

public:
    //...Constructor
    explicit XTide(QWebEngineView *inMap, mov_QChartView *inChart,
                   QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
                   QComboBox *inUnits, QStatusBar *inStatusBar, QObject *parent = 0);

    //...Destructor
    ~XTide();

    //...Public Functions
    int plotXTideStation();
    QString getLoadedXTideStation();
    QString getCurrentXTideStation();
    int getClickedXTideStation();
    int saveXTideData(QString filename, QString format);
    int saveXTidePlot(QString filename, QString filter);
    QString getErrorString();

private:
    int findXTideExe();
    int calculateXTides();
    int plotChart();
    int getDataBounds(double min, double max);
    int parseXTideResponse(QString xTideResponse);

    //Structures
    struct XTideStationData
    {
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
    QChart  *thisChart;

    //...Pointers to GUI elements
    QWebEngineView *map;
    mov_QChartView *chart;
    QDateEdit *startDateEdit,*endDateEdit;
    QComboBox *unitSelect;
    QStatusBar *statusBar;

};

#endif // XTIDE_H
