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
#include "mov_nefis.h"
#include <QDebug>

extern "C" {
#include "nefis_defines.h"
#include "btps.h"
#include "nefis.h"
}

mov_nefis::mov_nefis(QString defFilename, QString datFilename, QObject *parent) : QObject(parent)
{
    this->_mDefFilename = defFilename;
    this->_mDatFilename = datFilename;
}


QStringList mov_nefis::getSeriesNames()
{
    return this->_mSeriesNames;
}


QString mov_nefis::getSeriesDescription(QString seriesName)
{
    return this->_mSeriesDescriptionsMap[seriesName];
}


int mov_nefis::open()
{
    BInt4 ierr;
    BText datFile;
    BText defFile;

    //...Convert to char *
    datFile = strdup(this->_mDatFilename.toStdString().c_str());
    defFile = strdup(this->_mDefFilename.toStdString().c_str());

    //...Open the NEFIS file using machine defined endian-ness
    ierr = Crenef(&this->_fd,datFile,defFile,'M','r');

    //...Check for errors and set the open flag
    //   We could probably check fd in the close routine
    //   but having a logical is easier and future proof
    if(ierr==0)
    {
        this->_isOpen = true;
        //...Grab the initialization data
        //   e.g. station locations, series contained,
        //   and output times
        ierr = this->_init();
        return ierr;
    }
    else
    {
        this->_isOpen = false;
        return ierr;
    }


}


int mov_nefis::close()
{
    //...If the NEFIS file is open, close it and
    //   return the error code. If the file is not
    //   open return -1, but don't do something silly
    //   like try to close an unopened file.
    if(this->_isOpen)
        return Clsnef(&this->_fd);
    else
        return -1;
}


int mov_nefis::_init()
{
    this->_getStationLocations();
    this->_getSeriesList();
    this->_getTimes();

    return 0;
}


int mov_nefis::_getTimes()
{
    int       i,ierr;
    char  *   hisconst      = strdup("his-const");
    char  *   hisinfoseries = strdup("his-info-series");
    char  *   itdate        = strdup("ITDATE");
    char  *   ithisc        = strdup("ITHISC");
    char  *   dtc           = strdup("DT");
    char  *   tunitc        = strdup("TUNIT");
    double    dt,tunit,time;
    BInt4     uindex[MAX_NEFIS_DIM][3];
    BInt4     uorder[2];
    BRea4 *   buffer1 = (BRea4  *) malloc( sizeof(BRea4) * 1);
    BInt4 *   buffer2 = (BInt4  *) malloc( sizeof(BInt4) * 2);
    BInt4     buflen1 = sizeof(BRea4)*1;
    BInt4     buflen2 = sizeof(BInt4)*2;
    BInt4     nSteps;
    QDateTime initialTime;
    QString   tempString;

    uorder[0] = 1;
    uorder[1] = 2;
    uindex[0][0] = 1;
    uindex[0][1] = 1;
    uindex[0][2] = 1;

    //...Get the reference date
    ierr = Getelt(&this->_fd,hisconst,itdate,(BInt4 *)uindex,uorder,&buflen2,buffer2);
    if(ierr!=0)
    {
        free(buffer1);
        free(buffer2);
        return -1;
    }

    //...Convert the integer date to a QDateTime
    tempString.sprintf("%i",buffer2[0]);
    initialTime = QDateTime::fromString(tempString,QStringLiteral("yyyyMMdd"));
    initialTime.addSecs(buffer2[1]);
    initialTime.setTimeSpec(Qt::UTC);

    //...Find DT
    ierr = Getelt(&this->_fd,hisconst,dtc,(BInt4 *)uindex,uorder,&buflen1,buffer1);
    if(ierr!=0)
    {
        free(buffer1);
        free(buffer2);
        return -1;
    }
    dt = buffer1[0];

    //...Find TUINT
    ierr = Getelt(&this->_fd,hisconst,tunitc,(BInt4 *)uindex,uorder,&buflen1,buffer1);
    if(ierr!=0)
    {
        free(buffer1);
        free(buffer2);
        return -1;
    }
    tunit = buffer1[0];

    //...Find out how many steps are in the output
    ierr = Inqmxi(&this->_fd,hisinfoseries,&nSteps);
    if(ierr!=0)
    {
        free(buffer1);
        free(buffer2);
        return -1;
    }
    this->_mNumSteps = nSteps;

    //...Allocate a buffer for the data
    BInt4 * tsBuffer = (BInt4*)malloc(sizeof(BInt4)*nSteps);
    BInt4   buflents = sizeof(BInt4)*nSteps;

    uindex[0][0] = 1;
    uindex[0][1] = nSteps;
    uindex[0][2] = 1;

    //...Now read the time step for each output interval
    ierr = Getelt(&this->_fd,hisinfoseries,ithisc,(BInt4 *)uindex,uorder,&buflents,tsBuffer);
    if(ierr!=0)
    {
        free(tsBuffer);
        free(buffer1);
        free(buffer2);
        return -1;
    }

    //...Finally, construct a vector containing the output times
    this->_mOutputTimes.resize(nSteps);
    for(i=0;i<nSteps;i++)
    {
        time = (double)tsBuffer[i];
        this->_mOutputTimes[i] = initialTime.addSecs(time*dt*tunit);
    }

    free(tsBuffer);
    free(buffer1);
    free(buffer2);

    return 0;
}


int mov_nefis::_getStationLocations()
{
    int     i,j;
    BInt4   nSta;
    BInt4   realBuffSize,charBuffSize;
    BInt4   ierr;
    BInt4   uindex[MAX_NEFIS_DIM][3];
    BInt4   uorder[2];
    BInt4   charOrder[1];
    BRea4 * realDataBuffer;
    BText   elmtyp,elmunt,elmdes,elmqty;
    BInt4   elmndim = MAX_NEFIS_DIM;
    BInt4   elmnbyte;
    BInt4   elmdms[MAX_NEFIS_DIM];
    BChar   error_string[LENGTH_ERROR_MESSAGE];
    BChar   charDataBuffer[1000][20];
    QString tempString;

    //...The attributes we'll grab to get the stations
    char * hisconst = strdup("his-const");
    char * xystat   = strdup("XYSTAT");
    char * namst    = strdup("NAMST");

    //...Set up the indexing request, just default values here
    for(i=0;i<5;i++)
        for(j=0;j<3;j++)
            uindex[i][j] = 1;

    //...Ordering for reads
    uorder[0]  = 1;
    uorder[1]  = 2;

    //...Set to max, will be reset by NEFIS
    elmndim    = MAX_NEFIS_DIM;

    //...Malloc the character arrays
    elmtyp = (BChar*)malloc(sizeof(BChar)*MAX_NEFIS_TYPE );
    elmqty = (BChar*)malloc(sizeof(BChar)*MAX_NEFIS_NAME);
    elmunt = (BChar*)malloc(sizeof(BChar)*MAX_NEFIS_NAME);
    elmdes = (BChar*)malloc(sizeof(BChar)*MAX_NEFIS_DESC);

    //...Get the number of stations contained within the NEFIS file
    ierr = Inqelm(&this->_fd,xystat,elmqty,&elmnbyte,elmqty,elmunt,elmdes,&elmndim,elmdms);
    if(ierr!=0)
        return -1;
    nSta = elmdms[1];

    //...Sanity check
    if(nSta<1)
        return -1;

    //...Allocate the real data buffer
    realDataBuffer = (BRea4  *) malloc( sizeof(BRea4) *  nSta * 2);
    realBuffSize   = nSta*sizeof(BRea4)*2;

    //...Set the char data buffer
    charBuffSize = nSta*20;
    charOrder[0] = 1;

    //...Read the station locations from the NEFIS file
    ierr = Getelt(&this->_fd,hisconst,xystat,(BInt4 *)uindex,uorder,&realBuffSize,(BData)realDataBuffer);
    if(ierr!=0)
        return -1;

    //...Read the station names from the NEFIS file
    ierr = Getelt(&this->_fd,hisconst,namst,(BInt4 *)uindex,charOrder,&charBuffSize,(BData)charDataBuffer);
    if(ierr!=0)
        return -1;

    //...Save the stations into a vector for use later
    this->_mNumStations = nSta;
    this->_mStationLocations.resize(nSta);
    for(i=0;i<nSta;i++)
    {
        this->_mStationLocations[i] = QPointF(realDataBuffer[i*2],realDataBuffer[i*2+1]);
        tempString = QString::fromLocal8Bit(charDataBuffer[i]);
        this->_mStationNames.append(tempString.mid(0,20).simplified());
    }

    //...Free memory
    free(realDataBuffer);

    return 0;
}


int mov_nefis::_getSeriesList()
{
    int i,ierr;
    QVector<QString> tempNames,tempDesc,type;

    //...Read the his-series list
    ierr = this->_getSeriesNames(QStringLiteral("his-series"),tempNames,tempDesc,type);
    for(i=0;i<tempNames.size();i++)
    {
        this->_mSeriesNames.push_back(tempNames[i]);
        this->_mSeriesDescriptionsMap[tempNames[i]] = tempDesc[i];
        this->_mTypeMap[tempNames[i]] = type[i];
        this->_mSourceMap[tempNames[i]] = QStringLiteral("Delft3D-FLOW");
    }
    tempNames.clear();

    //...Read the his-wave-series list
    ierr = this->_getSeriesNames(QStringLiteral("his-wav-series"),tempNames,tempDesc,type);
    for(i=0;i<tempNames.size();i++)
    {
        this->_mSeriesNames.push_back(tempNames[i]);
        this->_mSeriesDescriptionsMap[tempNames[i]] = tempDesc[i];
        this->_mTypeMap[tempNames[i]] = type[i];
        this->_mSourceMap[tempNames[i]] = QStringLiteral("Delft3D-WAVE");
    }
    tempNames.clear();

    return 0;
}


int mov_nefis::_getSeriesNames(QString seriesGroup, QVector<QString> &seriesNames,
                               QVector<QString> &seriesDescriptions, QVector<QString> &seriesTypes)
{
    int i;
    BInt4 ierr;
    BInt4 nSteps;
    BInt4 nDimensions;
    BInt4 nByteSing;
    BInt4 grpDim = MAX_NEFIS_DIM;
    BInt4 celDim = MAX_NEFIS_CEL_DIM;
    BChar * hisseries = strdup(seriesGroup.toStdString().c_str());
    BChar elmNames[MAX_NEFIS_CEL_DIM][MAX_NEFIS_NAME+1];

    //...Allocate memory for variables read
    BInt4 * grpDms        = (BInt4  *) malloc( sizeof(BInt4) *  MAX_NEFIS_DIM );
    BInt4 * grpOrd        = (BInt4  *) malloc( sizeof(BInt4) *  MAX_NEFIS_DIM );
    BInt4 * elmDimensions = (BInt4  *) malloc( sizeof(BInt4) *  MAX_NEFIS_DIM );
    BText   celname       = (BText   ) malloc( sizeof(BChar) * (MAX_NEFIS_NAME + 1) );
    BText   type          = (BText   ) malloc( sizeof(BChar) * (MAX_NEFIS_TYPE + 1) );
    BText   quantity      = (BText   ) malloc( sizeof(BChar) * (MAX_NEFIS_NAME + 1) );
    BText   units         = (BText   ) malloc( sizeof(BChar) * (MAX_NEFIS_NAME + 1) );
    BText   description   = (BText   ) malloc( sizeof(BChar) * (MAX_NEFIS_DESC + 1) );

    //...Get the number of steps written to a station series
    ierr = Inqmxi(&this->_fd,hisseries,&nSteps);
    if(ierr!=0)
        return -1;

    //...Get the name of the cell containing the his-series data
    ierr = Inqgrp(&this->_fd,hisseries,celname,&grpDim,grpDms,grpOrd);
    if(ierr!=0)
        return -1;

    //...Get the list of elements inside the cell
    ierr = Inqcel(&this->_fd,celname,&celDim,elmNames);
    if(ierr!=0)
        return -1;

    //...Resize the output vectors
    seriesNames.resize(celDim);
    seriesDescriptions.resize(celDim);
    seriesTypes.resize(celDim);

    //...Get the descriptions of each element
    for(i=0;i<celDim;i++)
    {
        //...Save the element name for this series
        seriesNames[i] = QString(elmNames[i]).simplified();

        //...Retrieve the description of this series
        nDimensions = MAX_NEFIS_DIM;
        ierr = Inqelm(&this->_fd,elmNames[i],type,&nByteSing,quantity,units,description,&nDimensions,elmDimensions);
        if(ierr!=0)
            return -1;

        //...Save the element descriptions
        seriesDescriptions[i] = QString(description).simplified();

        //...Save the type (Integer or Real)
        seriesTypes[i] = QString(type).simplified();
    }

    return 0;
}


int mov_nefis::_get(QString seriesName)
{
    int i,j,ierr;
    char * src;
    BChar error_string[LENGTH_ERROR_MESSAGE];

    //...Allocate the variables used to retrieve from NEFIS
    BInt4   uindex[MAX_NEFIS_DIM][3];
    BInt4   uorder[2];
    BRea4 * realBuffer = (BRea4  *) malloc( sizeof(BRea4) * this->_mNumSteps * this->_mNumStations);
    BInt4 * intBuffer  = (BInt4  *) malloc( sizeof(BInt4) * this->_mNumSteps * this->_mNumStations);
    BInt4   realBufLen = sizeof(BRea4) * this->_mNumSteps * this->_mNumStations;
    BInt4   intBufLen  = sizeof(BInt4) * this->_mNumSteps * this->_mNumStations;

    //...Rectify the source to what it is named in the file
    if(this->_mSourceMap[seriesName]==QStringLiteral("Delft3D-FLOW"))
        src = strdup("his-series");
    else if(this->_mSourceMap[seriesName]==QStringLiteral("Delft3D-WAVE"))
        src = strdup("his-wav-series");
    else
    {
        free(realBuffer);
        free(intBuffer);
        return -1;
    }
    char * series = strdup(seriesName.toStdString().c_str());

    //...Clear the output container in case it was in use previously
    for(i=0;i<this->_mOutputData.size();i++)
        this->_mOutputData[i].clear();
    this->_mOutputData.clear();

    //...Reallocate the output container
    this->_mOutputData.resize(this->_mNumStations);
    for(i=0;i<this->_mOutputData.size();i++)
        this->_mOutputData[i].resize(this->_mNumSteps);


    //...Set up the indicies
    uorder[0] = 1;
    uorder[1] = 2;
    uorder[2] = 3;
    uindex[0][0] = 1;
    uindex[0][1] = this->_mNumSteps;
    uindex[0][2] = 1;
    uindex[1][0] = 1;
    uindex[1][1] = this->_mNumStations;
    uindex[1][2] = 1;

    //...Read the data from the file using the correct buffer style
    if(this->_mTypeMap[seriesName]==QStringLiteral("INTEGER"))
    {
        ierr = Getelt(&this->_fd,src,series,(BInt4 *)uindex,uorder,&intBufLen,intBuffer);
        if(ierr!=0)
        {
            free(realBuffer);
            free(intBuffer);
            return -1;
        }
        //...Put the data in the output array
        for(i=0;i<this->_mNumSteps;i++)
            for(j=0;j<this->_mNumStations;j++)
                this->_mOutputData[j][i] = (double)intBuffer[j+i*this->_mNumStations];
    }
    else if(this->_mTypeMap[seriesName]==QStringLiteral("REAL"))
    {
        ierr = Getelt(&this->_fd,src,series,(BInt4 *)uindex,uorder,&realBufLen,realBuffer);
        if(ierr!=0)
        {
            free(realBuffer);
            free(intBuffer);
            qDebug() << Neferr(2,error_string);
            return -1;
        }
        //...Put the data in the output array
        for(i=0;i<this->_mNumSteps;i++)
            for(j=0;j<this->_mNumStations;j++)
                this->_mOutputData[j][i] = realBuffer[j+i*this->_mNumStations];
    }

    //...Free memory
    free(realBuffer);
    free(intBuffer);

    return 0;
}


int mov_nefis::generateIMEDS(QString seriesName, imeds *stationData)
{
    int i,ierr;

    //...Check if the data exists
    if(!this->_mSeriesNames.contains(seriesName))
        return -1;

    //...Retrieve the data from NEFIS
    ierr = this->_get(seriesName);
    if(ierr!=0)
        return -1;

    //...Start constructing the IMEDS structure
    stationData->nstations = this->_mNumStations;
    stationData->station.resize(this->_mNumStations);
    stationData->header1 = this->_mSourceMap[seriesName];
    stationData->header2 = QStringLiteral("NEFIS");
    stationData->header3 = QStringLiteral("NEFIS");
    stationData->units   = QStringLiteral("metric");
    stationData->datum   = QStringLiteral("modelDatum");

    //...Loop over each station adding the data
    for(i=0;i<this->_mNumStations;i++)
    {
        stationData->station[i] = new imeds_station(this);
        stationData->station[i]->date = this->_mOutputTimes;
        stationData->station[i]->data = this->_mOutputData[i];
        stationData->station[i]->longitude = this->_mStationLocations[i].x();
        stationData->station[i]->latitude = this->_mStationLocations[i].y();
        stationData->station[i]->NumSnaps = this->_mNumSteps;
        stationData->station[i]->StationIndex = i+1;
        stationData->station[i]->StationName = this->_mStationNames[i];
        stationData->station[i]->StationID = QString::number(i+1);
        stationData->station[i]->isNull = false;
    }

    return 0;
}


QString mov_nefis::getNefisDatFilename(QString defFilename)
{
    return defFilename.mid(0,defFilename.length()-3)+"dat";
}


QString mov_nefis::getNefisDefFilename(QString datFilename)
{
    return datFilename.mid(0,datFilename.length()-3)+"def";
}
