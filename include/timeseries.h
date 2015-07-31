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
        
#ifndef TIMESERIES_H
#define TIMESERIES_H

#include <QMainWindow>
#include <QVector>
#include <QFile>
#include <QMessageBox>
#include <QDateTime>

//Data Sructures

//-------------------------------------------//
//Data contained within a single IMEDS
//station. This ends up wrapped in the IMEDS
//data structure.
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
//-------------------------------------------//

//-------------------------------------------//
//Wrapper for the IMEDS file format which
//will be used as the prototype throughout.
//All other formats end up converted to this.
struct IMEDS{
    int nstations;
    QString header1;
    QString header2;
    QString header3;
    QVector<IMEDS_DATA> station;
    bool success;
};
//-------------------------------------------//

//-------------------------------------------//
//Structure to hold ADCIRC data structure
//read from a netCDF formatted file
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
//-------------------------------------------//


//-------------------------------------------//
//Structure to hold the ADCIRC data structure
//read from an ASCII formatted file
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
//-------------------------------------------//


 //-------------------------------------------//
//Some variables used throughout
extern double FLAG_NULL_TS;
extern QDateTime FLAG_NULL_DATE;
extern QVector<IMEDS> TimeseriesData;
//-------------------------------------------//

//-------------------------------------------//
//Function prototypes
IMEDS readIMEDS(QString filename);

int readADCIRCnetCDF(QString filename, ADCNC &MyData);

ADCASCII readADCIRCascii(QString filename,
                         QString stationfile);

IMEDS NetCDF_to_IMEDS(ADCNC netcdf,
                      QDateTime Cold);

IMEDS ADCIRC_to_IMEDS(ADCASCII ASCII,
                      QDateTime Cold);

int GetUniqueStationList(QVector<IMEDS> Data,
                         QVector<double> &X,
                         QVector<double> &Y);

int BuildRevisedIMEDS(QVector<IMEDS> Data,
                      QVector<double> X,
                      QVector<double> Y,
                      QVector<IMEDS> &DataOut);
//-------------------------------------------//

#endif // TIMESERIES_H
