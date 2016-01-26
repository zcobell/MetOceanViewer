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
#ifndef USGS_H
#define USGS_H

#include <mov_errors.h>
#include <mov_flags.h>
#include <QtWebKitWidgets>
#include <QNetworkInterface>
#include <QUrl>
#include <QtNetwork>
#include <QVector>

class usgs : public QObject
{
public:
    usgs(QObject *parent = 0);

    ~usgs();

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

    //...Public functions
    int fetchUSGSData();
    int readUSGSDataFinished(QNetworkReply*);
    int formatUSGSInstantResponse(QByteArray Input);
    int formatUSGSDailyResponse(QByteArray Input);
    int plotUSGS(QString &javascript);

    //...Public varibales
    bool USGSDataReady;
    bool USGSBeenPlotted;
    int USGSdataMethod;
    int ProductIndex;
    double CurrentUSGSLat;
    double CurrentUSGSLon;
    QString USGSMarkerID;
    QString USGSMarkerString;
    QString CurrentUSGSStationName;
    QString USGSErrorString;
    QString ProductName;
    QDateTime requestStartDate;
    QDateTime requestEndDate;
    QVector<QString> Parameters;
    QVector<USGSStationData> USGSPlot;
    QVector<USGSData> CurrentUSGSStation;

private:
    int getTimezoneOffset(QString timezone);


};

#endif // USGS_H
