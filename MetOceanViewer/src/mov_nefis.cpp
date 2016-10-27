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
#include <iostream>

mov_nefis::mov_nefis(QString defFilename, QString datFilename, QObject *parent) : QObject(parent)
{
    this->mDefFilename = defFilename;
    this->mDatFilename = datFilename;
}


int mov_nefis::open()
{
    BInt4 ierr;
    BText datFile;
    BText defFile;

    datFile = strdup(this->mDatFilename.toStdString().c_str());
    defFile = strdup(this->mDefFilename.toStdString().c_str());

    ierr = Crenef(&this->fd,datFile,defFile,'M','r');

    if(ierr==0)
    {
        this->isOpen = true;
        return 0;
    }
    else
    {
        this->isOpen = false;
        return ierr;
    }
}


int mov_nefis::close()
{      
    if(this->isOpen)
        return Clsnef(&this->fd);
    else
        return 0;
}


int mov_nefis::getStationData()
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
    elmtyp = (BChar*)malloc(sizeof(BChar)*8 );
    elmqty = (BChar*)malloc(sizeof(BChar)*16);
    elmunt = (BChar*)malloc(sizeof(BChar)*16);
    elmdes = (BChar*)malloc(sizeof(BChar)*64);

    //...Get the number of stations contained within the NEFIS file
    ierr = Inqelm(&this->fd,xystat,elmqty,&elmnbyte,elmqty,elmunt,elmdes,&elmndim,elmdms);
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
    ierr = Getelt(&this->fd,hisconst,xystat,(BInt4 *)uindex,uorder,&realBuffSize,(BData)realDataBuffer);
    if(ierr!=0)
        return -1;

    //...Read the station names from the NEFIS file
    ierr = Getelt(&this->fd,hisconst,namst,(BInt4 *)uindex,charOrder,&charBuffSize,(BData)charDataBuffer);
    if(ierr!=0)
        return -1;

    //...Save the stations into a vector for use later
    this->mNumStations = nSta;
    this->mStationLocations.resize(nSta);
    this->mStationNames.resize(nSta);
    for(i=0;i<nSta;i++)
    {
        this->mStationLocations[i] = QPointF(realDataBuffer[i*2],realDataBuffer[i*2+1]);
        tempString = QString::fromLocal8Bit(charDataBuffer[i]);
        this->mStationNames[i] = tempString.mid(0,20).simplified();
    }

    //...Free memory
    free(realDataBuffer);
    free(elmtyp);
    free(elmqty);
    free(elmunt);
    free(elmdes);

    return 0;
}
