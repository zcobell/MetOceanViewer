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

#include <mov_errors.h>
#include <mov_flags.h>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>
#include <QChartView>
#include <QtCharts>
#include <general_functions.h>

using namespace QtCharts;

class noaa : public QObject
{
    Q_OBJECT
public:
    //...Constructor
    explicit noaa(QObject *parent = 0);

    //...Destructor
    ~noaa();

    //...Public Functions
    int fetchNOAAData();
    int prepNOAAResponse();
    int retrieveProduct(int type, QString &Product, QString &Product2);
    int getDataBounds(double &ymin, double &ymax);
    int generateLabels();
    int plotChart(QChartView *chartView);

    //...Structures
    struct NOAAStationData
    {
        QDate Date;
        QTime Time;
        double value;
    };

    //...Public Variables
    int NOAAMarkerID;
    int ProductIndex;

    double CurrentNOAALat;
    double CurrentNOAALon;

    QString CurrentNOAAStationName;
    QString Datum;
    QString Units;
    QString yLabel;
    QString plotTitle;

    QDateTime StartDate;
    QDateTime EndDate;

    QVector< QVector<NOAAStationData> > CurrentNOAAStation;
    QVector<QString> ErrorString;

private:

    //...Private Functions
    QString formatNOAAResponse(QVector<QByteArray> Input, QString &ErrorString, int index);
    void readNOAAResponse(QNetworkReply *reply, int index, int index2);

    //...Private Variables
    QVector< QVector<QByteArray> > NOAAWebData;

};

#endif // NOAA_H
