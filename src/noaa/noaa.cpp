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
#include "noaa.h"

noaa::noaa(QObject *parent)
{

}

noaa::~noaa()
{

}


double noaa::niceNumber(double number, bool round)
{
    int exp  = qFloor(qLn(number)/qLn(10.0));
    double f = number / qPow(10,exp);
    if(round)
    {
        if(f<1.5)
            return qPow(10,exp);
        else if(f<3.0)
            return 2*qPow(10,exp);
        else if(f<7.0)
            return 5*qPow(10,exp);
        else
            return 10*qPow(10,exp);
    }
    else
    {
        if(f<=1.0)
            return qPow(10,exp);
        else if(f<=2.0)
            return 2*qPow(10,exp);
        else if(f<=5.0)
            return 5*qPow(10,exp);
        else
            return 10*qPow(10,exp);
    }
}


QVector<double> noaa::niceLabels(double inMin, double inMax, int nTicks, int &nFrac)
{
    double range    = niceNumber(qMax(qAbs(inMax),qAbs(inMin)),false);
    double d        = niceNumber(range/(nTicks-1),false);
    double graphMin = qFloor(inMin/d)*d;
    double graphMax = qCeil(inMax/d)*d;
    nFrac = qMax(-qFloor(qLn(d)/qLn(10)),0);

    int idx = 0;
    QVector<double> output;
    for(double i=graphMin;i<graphMax+0.5*d;i=i+d)
    {
        idx = idx + 1;
        output.resize(idx);
        output[idx-1] = i;
    }
    return output;
}
