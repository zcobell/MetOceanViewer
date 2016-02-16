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
#include <netcdf.h>

int user_timeseries::readIMEDS(QString filename,IMEDS &Output)
{

    //Variables
    QString year;
    QString month;
    QString day;
    QString hour;
    QString minute;
    QString second;
    QStringList TempList;
    QVector<QString> FileData;
    int nLine;
    int nStation;
    int i;
    int j;
    int k;
    int expectedLength;
    double value;
    QFile MyFile(filename);

    //Default to an unsuccessful read
    Output.success = false;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
        return -1;
    }

    //Read the header to output variable

    Output.header1 = MyFile.readLine().simplified();
    Output.header2 = MyFile.readLine().simplified();
    Output.header3 = MyFile.readLine().simplified();

    //Read in the data portion of the file
    nLine = 0;
    while(!MyFile.atEnd())
    {
       nLine = nLine + 1;
       FileData.resize(nLine);
       FileData[nLine-1] = MyFile.readLine().simplified();
    }
    MyFile.close();

    //Count the number of stations in the file
    nStation = 0;
    for(i=0;i<nLine;i++)
    {
        TempList = FileData[i].split(" ");
        if(TempList.length()==3)
        {
            nStation = nStation + 1;
        }
    }

    //Size the station vector and read the datasets
    Output.station.resize(nStation);
    Output.nstations = nStation;

    //Zero out the incremented variable
    for(i=0;i<nStation;i++)
        Output.station[i].NumSnaps = 0;

    //Organize the data into the variable
    j=-1;
    for(i=0;i<nLine;i++)
    {
        TempList = FileData[i].split(" ");
        if(TempList.length()==3)
        {
            j=j+1;
            Output.station[j].longitude = TempList[2].toDouble();
            Output.station[j].latitude = TempList[1].toDouble();
            Output.station[j].StationName = TempList[0];
            Output.station[j].StationIndex = j;
        }
        else
        {
            Output.station[j].NumSnaps = Output.station[j].NumSnaps + 1;
        }

    }
    //Preallocate arrays for data and dates

    for(i=0;i<nStation;i++)
    {
        Output.station[i].data.resize(Output.station[i].NumSnaps);
        Output.station[i].date.resize(Output.station[i].NumSnaps);
    }

    //Now, loop over the data section and save to vectors
    j=-1;
    k=-1;
    expectedLength = 0;
    for(i=0;i<nLine;i++)
    {
        TempList = FileData[i].split(" ");
        if(TempList.length()==3)
        {
            j=j+1;
            k=-1;
        }
        else
        {
            if(k==-1)
            {
                if(TempList.length()==6)
                {
                    expectedLength=6;
                    k=k+1;
                    year = TempList.value(0);
                    month = TempList.value(1);
                    day = TempList.value(2);
                    hour = TempList.value(3);
                    minute = TempList.value(4);
                    second = "0";
                    value = TempList.value(5).toDouble();
                    Output.station[j].date[k] =
                            QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                      QTime(hour.toInt(),minute.toInt(),second.toInt()));
                    Output.station[j].date[k].setTimeSpec(Qt::UTC);
                    Output.station[j].data[k] = value;
                    Output.success = true;
                }
                else if(TempList.length()==7)
                {
                    expectedLength=7;
                    k=k+1;
                    year = TempList.value(0);
                    month = TempList.value(1);
                    day = TempList.value(2);
                    hour = TempList.value(3);
                    minute = TempList.value(4);
                    second = TempList.value(5);
                    value = TempList.value(6).toDouble();
                    Output.station[j].date[k] =
                            QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                      QTime(hour.toInt(),minute.toInt(),second.toInt()));
                    Output.station[j].date[k].setTimeSpec(Qt::UTC);
                    Output.station[j].data[k] = value;
                    Output.success = true;
                }
            }
            else
            {
                if(expectedLength!=TempList.length())
                {
                    Output.success = false;
                    return -1;
                }

                if(expectedLength==6)
                {
                    k=k+1;
                    year = TempList.value(0);
                    month = TempList.value(1);
                    day = TempList.value(2);
                    hour = TempList.value(3);
                    minute = TempList.value(4);
                    second = "0";
                    value = TempList.value(5).toDouble();
                    Output.station[j].date[k] =
                            QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                      QTime(hour.toInt(),minute.toInt(),second.toInt()));
                    Output.station[j].date[k].setTimeSpec(Qt::UTC);
                    Output.station[j].data[k] = value;
                    Output.success = true;
                }
                else if(expectedLength==7)
                {
                    expectedLength=7;
                    k=k+1;
                    year = TempList.value(0);
                    month = TempList.value(1);
                    day = TempList.value(2);
                    hour = TempList.value(3);
                    minute = TempList.value(4);
                    second = TempList.value(5);
                    value = TempList.value(6).toDouble();
                    Output.station[j].date[k] =
                            QDateTime(QDate(year.toInt(),month.toInt(),day.toInt()),
                                      QTime(hour.toInt(),minute.toInt(),second.toInt()));
                    Output.station[j].date[k].setTimeSpec(Qt::UTC);
                    Output.station[j].data[k] = value;
                    Output.success = true;
                }
            }
        }
    }
    return 0;
}

//-------------------------------------------//


//-------------------------------------------//
//Read an ADCIRC netCDF file
//-------------------------------------------//
int user_timeseries::readADCIRCnetCDF(QString filename, ADCNC &MyData)
{

    size_t station_size,time_size,startIndex;
    int i,j,time_size_int,station_size_int;
    int ierr, ncid, varid_zeta, varid_zeta2, varid_lat, varid_lon, varid_time;
    int dimid_time,dimid_station;
    bool isVector;
    double Temp;
    QVector<double> readData1;
    QVector<double> readData2;

    //Size the location array
    size_t start[2];

    QVector<QString> netcdf_types;
    netcdf_types.resize(6);
    netcdf_types[0] = "zeta";
    netcdf_types[1] = "u-vel";
    netcdf_types[2] = "v-vel";
    netcdf_types[3] = "pressure";
    netcdf_types[4] = "windx";
    netcdf_types[5] = "windy";

    //Open the file
    ierr = nc_open(filename.toUtf8(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    //Get the dimension ids
    ierr = nc_inq_dimid(ncid,"time",&dimid_time);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_dimid(ncid,"station",&dimid_station);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    //Find out the dimension size
    ierr = nc_inq_dimlen(ncid,dimid_time,&time_size);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_dimlen(ncid,dimid_station,&station_size);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    station_size_int = static_cast<unsigned int>(station_size);
    time_size_int = static_cast<unsigned int>(time_size);

    //Find the variable in the NetCDF file
    for(i=0;i<6;i++)
    {
        ierr = nc_inq_varid(ncid,netcdf_types[i].toUtf8(),&varid_zeta);

        //If we found the variable, we're done
        if(ierr==NC_NOERR)
        {
            if(i==1)
            {
                isVector = true;
                ierr = nc_inq_varid(ncid,netcdf_types[i+1].toUtf8(),&varid_zeta2);
                if(ierr!=NC_NOERR)
                {
                    MyData.err = ierr;
                    MyData.success = false;
                    return -1;
                }
            }
            else
                isVector = false;

            MyData.DataType=netcdf_types[i];
            break;
        }

        //If we're at the end of the array
        //and haven't quit yet, that's a problem
        if(i==5)
        {
            MyData.err = ierr;
            MyData.success = false;
            return -1;
        }
    }

    //Size the output variables
    MyData.latitude.resize(station_size_int);
    MyData.longitude.resize(station_size_int);
    MyData.nstations = station_size_int;
    MyData.NumSnaps = time_size_int;
    MyData.time.resize(time_size_int);
    MyData.data.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        MyData.data[i].resize(time_size_int);

    //Read the station locations and times
    ierr = nc_inq_varid(ncid,"time",&varid_time);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_varid(ncid,"x",&varid_lon);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }
    ierr = nc_inq_varid(ncid,"y",&varid_lat);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    for(j=0;j<time_size_int;j++)
    {
        startIndex = static_cast<size_t>(j);
        ierr = nc_get_var1(ncid,varid_time,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return -1;
        }
        MyData.time[j] = Temp;
    }

    for(j=0;j<station_size_int;j++)
    {
        startIndex = static_cast<size_t>(j);
        ierr = nc_get_var1(ncid,varid_lon,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return -1;
        }
        MyData.longitude[j] = Temp;

        ierr = nc_get_var1(ncid,varid_lat,&startIndex,&Temp);
        if(ierr!=NC_NOERR)
        {
            MyData.success = false;
            MyData.err = ierr;
            return -1;
        }
        MyData.latitude[j] = Temp;
    }

    readData1.resize(time_size_int);
    readData2.resize(time_size_int);

    //Loop over the stations, reading the data into memory
    for(i=0;i<station_size_int;++i)
    {


        //Read from NetCDF
        for(j=0;j<time_size_int;j++)
        {
            start[0] = static_cast<size_t>(j);
            start[1] = static_cast<size_t>(i);
            ierr = nc_get_var1(ncid,varid_zeta,start,&Temp);
            if(ierr!=NC_NOERR)
            {
                MyData.success = false;
                MyData.err = ierr;
                return -1;
            }
            readData1[j] = Temp;
        }

        if(isVector)
        {
            for(j=0;j<time_size_int;j++)
            {
                start[0] = static_cast<size_t>(j);
                start[1] = static_cast<size_t>(i);
                ierr = nc_get_var1(ncid,varid_zeta2,start,&Temp);
                if(ierr!=NC_NOERR)
                {
                    MyData.success = false;
                    MyData.err = ierr;
                    return -1;
                }
                readData2[j] = Temp;
                MyData.data[i][j] = qSqrt(qPow(readData1[j],2.0)+qPow(readData2[j],2.0));
            }
        }
        else
        {
            //Place in the output variable
            for(j=0;j<time_size_int;++j)
                MyData.data[i][j] = readData1[j];
        }
    }
    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        MyData.success = false;
        MyData.err = ierr;
        return -1;
    }

    //Finally, name the stations the default names for now. Later
    //we can get fancy and try to get the ADCIRC written names in
    //the NetCDF file
    MyData.station_name.resize(station_size_int);
    for(i=0;i<station_size_int;++i)
        MyData.station_name[i] = "Station "+QString::number(i);

    MyData.success = true;

    return 0;
}
//-------------------------------------------//


//-------------------------------------------//
//Convert the netCDF ADCIRC variable to an
//IMEDS style variable
//-------------------------------------------//
int user_timeseries::NetCDF_to_IMEDS(ADCNC netcdf, QDateTime Cold, IMEDS &Output)
{

    Output.nstations = netcdf.nstations;
    Output.station.resize(netcdf.nstations);
    for(int i=0;i<Output.nstations;++i)
    {
        Output.station[i].latitude = netcdf.latitude[i];
        Output.station[i].longitude = netcdf.longitude[i];
        Output.station[i].NumSnaps = netcdf.NumSnaps;
        Output.station[i].StationIndex = i;
        Output.station[i].StationName = netcdf.station_name[i];
        Output.station[i].data.resize(Output.station[i].NumSnaps);
        Output.station[i].date.resize(Output.station[i].NumSnaps);
        for(int j=0;j<Output.station[i].NumSnaps;++j)
        {
            Output.station[i].data[j] = netcdf.data[i][j];
            Output.station[i].date[j] = Cold.addSecs(netcdf.time[j]);
        }
    }
    Output.success = true;
    return -1;

}
//-------------------------------------------//


//-------------------------------------------//
//Read an ADCIRC ASCII file
//-------------------------------------------//
int user_timeseries::readADCIRCascii(QString filename, QString stationfile, ADCASCII &MyData)
{
    QFile MyFile(filename), StationFile(stationfile);
    QString header1, header2, TempLine;
    QStringList headerData, TempList, TempList2;

    MyData.success = false;

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+MyFile.errorString());
        MyFile.close();
        return -1;
    }
    if(!StationFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR","ERROR:"+StationFile.errorString());
        StationFile.close();
        return -1;
    }


    //Read the 61/62 style file
    header1 = MyFile.readLine();
    header2 = MyFile.readLine().simplified();
    headerData = header2.split(" ");

    MyData.NumSnaps = headerData.value(0).toInt();
    MyData.nstations = headerData.value(1).toInt();
    MyData.OutputTimeFreq = headerData.value(2).toDouble();
    MyData.OutputTSFreq = headerData.value(3).toInt();
    MyData.NumColumns = headerData.value(4).toInt();

    MyData.time.resize(MyData.NumSnaps);
    MyData.data.resize(MyData.nstations);

    for(int i=0;i<MyData.nstations;++i)
        MyData.data[i].resize(MyData.NumSnaps);

    for(int i=0;i<MyData.NumSnaps;++i)
    {
        TempLine = MyFile.readLine().simplified();
        TempList = TempLine.split(" ");
        MyData.time[i] = TempList.value(1).toDouble();
        for(int j=0;j<MyData.nstations;++j)
        {
            TempLine = MyFile.readLine().simplified();
            TempList = TempLine.split(" ");
            MyData.data[j][i] = TempList.value(1).toDouble();
            if(MyData.NumColumns==2)
                MyData.data[j][i] = qPow(qPow(MyData.data[j][i],2) +
                                         qPow(TempList.value(2).toDouble(),2),2);
        }
    }
    MyFile.close();

    //Now read the station location file
    TempLine = StationFile.readLine().simplified();
    TempList = TempLine.split(" ");
    int TempStations = TempList.value(0).toInt();
    if(TempStations!=MyData.nstations)
    {
        QMessageBox::information(NULL,"ERROR","The number of stations do not match in both files");
        return -1;
    }

    MyData.longitude.resize(MyData.nstations);
    MyData.latitude.resize(MyData.nstations);
    MyData.station_name.resize(MyData.nstations);

    for(int i=0;i<TempStations;++i)
    {
        TempLine = StationFile.readLine().simplified();
        TempList = TempLine.split(QRegExp(",| "));
        MyData.longitude[i] = TempList.value(0).toDouble();
        MyData.latitude[i] = TempList.value(1).toDouble();

        if(TempList.length()>2)
        {
            MyData.station_name[i] = "";
            for(int j=2;j<TempList.length();++j)
                MyData.station_name[i] = MyData.station_name[i]+" "+TempList.value(j);
        }
        else
            MyData.station_name[i] = "Station_"+QString::number(i);

    }
    StationFile.close();

    MyData.success = true;
    return -1;
}
//-------------------------------------------//


//-------------------------------------------//
//Convert an ADCIRC ASCII file to an IMEDS
//style file
//-------------------------------------------//
int user_timeseries::ADCIRC_to_IMEDS(ADCASCII ASCII, QDateTime Cold, IMEDS &MyOutput)
{

    MyOutput.nstations = ASCII.nstations;
    MyOutput.station.resize(MyOutput.nstations);

    for(int i=0;i<MyOutput.nstations;++i)
    {
        MyOutput.station[i].data.resize(ASCII.NumSnaps);
        MyOutput.station[i].date.resize(ASCII.NumSnaps);
        MyOutput.station[i].StationName = ASCII.station_name[i];
        MyOutput.station[i].NumSnaps = ASCII.NumSnaps;
        MyOutput.station[i].longitude = ASCII.longitude[i];
        MyOutput.station[i].latitude = ASCII.latitude[i];
        MyOutput.station[i].StationIndex = i;
        for(int j=0;j<ASCII.NumSnaps;++j)
        {
            MyOutput.station[i].date[j] = Cold.addSecs(ASCII.time[j]);
            MyOutput.station[i].data[j] = ASCII.data[i][j];
        }
    }
    MyOutput.success = true;
    return -1;
}
//-------------------------------------------//


//-------------------------------------------//
//Generate a unique list of stations so that
//we can later build a complete list of stations
//and not show data where it doesn't exist for
//certain files
//-------------------------------------------//
int user_timeseries::GetUniqueStationList(QVector<IMEDS> Data, QVector<double> &X, QVector<double> &Y)
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
int user_timeseries::BuildRevisedIMEDS(QVector<IMEDS> Data,QVector<double> X, QVector<double> Y, QVector<IMEDS> &DataOut)
{
    int i,j,k;
    bool found;

    QDateTime NullDate(QDate(MOV_NULL_YEAR,MOV_NULL_MONTH,MOV_NULL_DAY),QTime(MOV_NULL_HOUR,MOV_NULL_MINUTE,MOV_NULL_SECOND));

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
                DataOut[i].station[j].date[0] = NullDate;
                DataOut[i].station[j].isNull = true;
            }
        }
    }
    return 0;
}
