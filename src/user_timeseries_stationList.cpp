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
#include <user_timeseries.h>
#include <mov_flags.h>

//-------------------------------------------//
//Generate a unique list of stations so that
//we can later build a complete list of stations
//and not show data where it doesn't exist for
//certain files
//-------------------------------------------//
int user_timeseries::getUniqueStationList(QVector<IMEDS> Data, QVector<double> &X, QVector<double> &Y)
{
    int i,j,k;
    bool found;
    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<Data[i].nstations;j++)
        {
            found = false;
            for(k=0;k<X.length();k++)
            {
                if(Data[i].station[j].longitude == X[k] &&
                        Data[i].station[j].latitude == Y[k] )
                {
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                X.resize(X.length()+1);
                Y.resize(Y.length()+1);
                X[X.length()-1] = Data[i].station[j].longitude;
                Y[Y.length()-1] = Data[i].station[j].latitude;
            }

        }
    }
    return 0;
}
//-------------------------------------------//


//-------------------------------------------//
//Build a revised set of IMEDS data series
//which will have null data where there was
//not data in the file
//-------------------------------------------//
int user_timeseries::buildRevisedIMEDS(QVector<IMEDS> Data,QVector<double> X, QVector<double> Y, QVector<IMEDS> &DataOut)
{
    int i,j,k;
    bool found;

    DataOut.resize(Data.length());
    for(i=0;i<Data.length();i++)
    {
        DataOut[i].nstations = X.length();
        DataOut[i].header1 = Data[i].header1;
        DataOut[i].header2 = Data[i].header2;
        DataOut[i].header3 = Data[i].header3;
        DataOut[i].station.resize(X.length());
        for(j=0;j<X.length();j++)
        {
            DataOut[i].station[j].longitude = X[j];
            DataOut[i].station[j].latitude = Y[j];
        }
    }

    for(i=0;i<Data.length();i++)
    {
        for(j=0;j<DataOut[i].nstations;j++)
        {
            found = false;
            for(k=0;k<Data[i].nstations;k++)
            {
                if(Data[i].station[k].longitude == DataOut[i].station[j].longitude &&
                   Data[i].station[k].latitude == DataOut[i].station[j].latitude)
                {
                    DataOut[i].station[j].data.resize(Data[i].station[k].data.length());
                    DataOut[i].station[j].date.resize(Data[i].station[k].date.length());
                    DataOut[i].station[j].NumSnaps = Data[i].station[k].NumSnaps;
                    DataOut[i].station[j].StationName = Data[i].station[k].StationName;
                    DataOut[i].station[j].data = Data[i].station[k].data;
                    DataOut[i].station[j].date = Data[i].station[k].date;
                    DataOut[i].station[j].isNull = false;
                    found = true;
                    break;
                }
            }
            if(!found)
            {
                //Build a station with a null dataset we can find later
                DataOut[i].station[j].data.resize(1);
                DataOut[i].station[j].date.resize(1);
                DataOut[i].station[j].StationName = "NONAME";
                DataOut[i].station[j].data[0] = MOV_NULL_TS;
                DataOut[i].station[j].date[0] = QDateTime(QDate(MOV_NULL_YEAR,MOV_NULL_MONTH,MOV_NULL_DAY),
                                                          QTime(MOV_NULL_HOUR,MOV_NULL_MINUTE,MOV_NULL_SECOND));
                DataOut[i].station[j].isNull = true;
            }
        }
    }
    return 0;
}
//-------------------------------------------//

