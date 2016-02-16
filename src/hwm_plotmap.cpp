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
#include <hwm.h>
#include <general_functions.h>

int hwm::plotHWMMap()
{

    QString MeasuredString,ModeledString,Marker,MyClassList;
    QString unitString;
    double x,y,measurement,modeled,error,MaximumValue;
    int i,classification,units;

    units = this->unitComboBox->currentIndex();
    if(units==1)
        unitString = "'m'";
    else
        unitString = "'ft'";

    //Plot the high water mark map
    MeasuredString = "";
    ModeledString = "";
    MaximumValue = 0;

    //Make sure we are fresh for if this is a second round of plotting
    this->map->page()->runJavaScript("clearMarkers()");

    //Give the browsers a chance to catch up to us
    delay(1);

    for(i=0;i<this->highWaterMarks.length();i++)
    {
        x = this->highWaterMarks[i].lon;
        y = this->highWaterMarks[i].lat;
        measurement = this->highWaterMarks[i].measured;
        modeled = this->highWaterMarks[i].modeled;
        error = this->highWaterMarks[i].error;

        if(modeled < -9999)
            classification = -1;
        else
            classification = this->classifyHWM(error);

        if(measurement > MaximumValue)
            MaximumValue = measurement + 1;
        else if(modeled > MaximumValue)
            MaximumValue = modeled + 1;

        Marker = "addHWM("+QString::number(x)+","+QString::number(y)+
                ","+QString::number(i)+","+QString::number(modeled)+","+QString::number(measurement)+
                ","+QString::number(error)+","+QString::number(classification)+","+unitString+
                ")";
        this->map->page()->runJavaScript(Marker);
        if(i==0)
        {
            ModeledString = QString::number(modeled);
            MeasuredString = QString::number(measurement);
        }
        else
        {
            ModeledString = ModeledString+":"+QString::number(modeled);
            MeasuredString = MeasuredString+":"+QString::number(measurement);
        }
    }

    MyClassList = "addLegend("+unitString+",'"+QString::number(classes[0],'f',2)+":"+QString::number(classes[1],'f',2)+":"+
            QString::number(classes[2],'f',2)+":"+QString::number(classes[3],'f',2)+":"+
            QString::number(classes[4],'f',2)+":"+QString::number(classes[5],'f',2)+":"+
            QString::number(classes[6],'f',2)+"')";
    this->map->page()->runJavaScript(MyClassList);

    return 0;
}

