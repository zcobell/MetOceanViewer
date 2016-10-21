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
    BInt4 ierr;


    return 0;
}
