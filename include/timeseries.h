//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2015.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS" 
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: timeseries.h
//
//------------------------------------------------------------------------------


        
#ifndef TIMESERIES_H
#define TIMESERIES_H

#include <QMainWindow>
#include <QVector>
#include <QFile>
#include <QMessageBox>
#include <QDateTime>

//Data Sructures
struct IMEDS_DATA
{
    double              latitude;
    double              longitude;
    QString             StationName;
    int                 NumSnaps;
    int                 StationIndex;
    QVector<QDateTime>  date;
    QVector<double>     data;
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


//Function prototypes
IMEDS readIMEDS(QString filename);

ADCNC readADCIRCnetCDF(QString filename);

ADCASCII readADCIRCascii(QString filename, QString stationfile);

IMEDS NetCDF_to_IMEDS(ADCNC netcdf, QDateTime Cold);

IMEDS ADCIRC_to_IMEDS(ADCASCII ASCII, QDateTime Cold);

//Data holder
extern QVector<IMEDS> IMEDSData;

#endif // TIMESERIES_H
