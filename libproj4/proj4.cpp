//-----GPL----------------------------------------------------------------------
//
// This file is part of MetOceanViewer
// Copyright (C) 2015-2017 Zach Cobell
//
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//------------------------------------------------------------------------------
//
//  File: proj4.cpp
//
//------------------------------------------------------------------------------
#include "proj4.h"
#include "proj_api.h"
#include "epsg.h"

//-----------------------------------------------------------------------------------------//
// Initializer
//-----------------------------------------------------------------------------------------//
/** \brief Constructor for the proj4 wrapper class
 *
 **/
//-----------------------------------------------------------------------------------------//
proj4::proj4(QObject *parent)
{

}
//-----------------------------------------------------------------------------------------//


//-----------------------------------------------------------------------------------------//
// Function to execute a coordinate system transformation using Proj4
//-----------------------------------------------------------------------------------------//
/** \brief Function to execute a coordinate system transformation using Proj4
 *
 * Function to execute a coordinate system transformation using Proj4
 *
 **/
//-----------------------------------------------------------------------------------------//
int proj4::transform(int inputEPSG, int outputEPSG, double x_in, double y_in, double &x_out, double &y_out, bool &isLatLon)
{
    projPJ inputPJ,outputPJ;
    double x,y,z;
    int ierr;

    ierr = 0;
    z = 0.0;

    if(!epsgMapping.contains(inputEPSG))
        return ERROR_PROJ4_NOSUCHPROJECTION;

    if(!epsgMapping.contains(outputEPSG))
        return ERROR_PROJ4_NOSUCHPROJECTION;

    QString currentInitialization = epsgMapping[inputEPSG];
    QString outputInitialization  = epsgMapping[outputEPSG];

    if(!(inputPJ = pj_init_plus(currentInitialization.toStdString().c_str())))
        return ERROR_PROJ4_INTERNAL;

    if(!(outputPJ = pj_init_plus(outputInitialization.toStdString().c_str())))
        return ERROR_PROJ4_INTERNAL;

    if(pj_is_latlong(inputPJ))
    {
        x = x_in*DEG_TO_RAD;
        y = y_in*DEG_TO_RAD;
    }
    else
    {
        x = x_in;
        y = y_in;
    }

    ierr = pj_transform(inputPJ,outputPJ,1,1,&x,&y,&z);

    if(ierr!=0)
        return ERROR_PROJ4_INTERNAL;

    if(pj_is_latlong(outputPJ))
    {
        x_out = x*RAD_TO_DEG;
        y_out = y*RAD_TO_DEG;
        isLatLon = true;
    }
    else
    {
        x_out = x;
        y_out = y;
        isLatLon = false;
    }

    return ERROR_NOERROR;
}
//-----------------------------------------------------------------------------------------//


//-----------------------------------------------------------------------------------------//
// Function to return the internal map constructed for outside usage
//-----------------------------------------------------------------------------------------//
/** \brief Function to return the internal map constructed for outside usage
 *
 * Function to return the internal map constructed for outside usage
 *
 **/
//-----------------------------------------------------------------------------------------//
QMap<int, QString> *proj4::getMap()
{
    return &epsgMapping;
}
//-----------------------------------------------------------------------------------------//
