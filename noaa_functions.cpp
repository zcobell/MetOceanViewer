//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
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
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: noaa_functions.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"
#include "imeds.h"

//Routine that fires after the NOAA data is read from the server
void MainWindow::ReadNOAADataFinished(QNetworkReply *reply)
{
    QByteArray NOAAWebData;
    QString NOAAData,javastring;
    QString Datum,Units,Product;
    int ProductIndex;

    Units = ui->combo_noaaunits->currentText();
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    Product = retrieveProduct(1);

    if(ProductIndex == 0 || ProductIndex == 1)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        Datum = ui->combo_noaadatum->currentText();
    }
    else if(ProductIndex == 2 || ProductIndex == 5)
    {
        if(Units=="metric")
            Units="m/s";
        else
            Units="ft/s";
        Datum = "Stnd";
    }
    else if(ProductIndex == 3 || ProductIndex == 4)
    {
        if(Units=="metric")
            Units="Celcius";
        else
            Units="Fahrenheit";
        Datum = "Stnd";
    }
    else if(ProductIndex == 6)
    {
        Units = "%";
        Datum = "Stnd";
    }
    else if(ProductIndex == 7)
    {
        Units = "mb";
        Datum = "Stnd";
    }


    if(reply->error()!=0)
    {
        QMessageBox::information(this,"ERROR","ERROR: "+reply->errorString());
        return;
    }
    NOAAWebData=reply->readAll();
    NOAAData = FormatNOAAResponse(NOAAWebData);

    javastring="drawNOAAData("+NOAAData+",'"+Datum+"','"+Units+"','"+Product+"')";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);

}

//Routine that draws the markers on the NOAA map
void MainWindow::drawMarkers(bool DelayDraw)
{
    int i,ID;
    double X,Y;
    QVariant MyVar;
    QString javastring,StationName;


    //Make the requests for the all the stations
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");

    if(DelayDraw)
        delay(2); //Give the map a chance to set up

    for(i=0;i<NOAAStations[0].size();i++)
    {
        X  = NOAAStations[1][i];
        Y  = NOAAStations[2][i];
        ID = static_cast<int>(NOAAStations[0][i]);
        StationName = NOAAStationNames[i];
        //Plot a station
        javastring = "addNOAAStation("+QString::number(X)+","+QString::number(Y)+","+QString::number(ID)
                +","+QString::number(i)+",'"+StationName+"')";
        MyVar = ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);
    }
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("readyToPlay()");
    return;
}

//Routine that starts to read the list of NOAA station locations
void MainWindow::BeginGatherStations()
{
    int i,NumStations;
    QString MyLine,TempString;
    QStringList MyList;
    QFile StationFile(":/data/NOAA_StationList.txt");

    if(!StationFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        QMessageBox::information(NULL,"ERROR",StationFile.errorString());
        return;
    }

    NOAAStations.resize(3);
    i = 0;

    MyLine = StationFile.readLine().simplified();
    NumStations = MyLine.toInt();
    NOAAStations[0].resize(NumStations);
    NOAAStations[1].resize(NumStations);
    NOAAStations[2].resize(NumStations);
    NOAAStationNames.resize(NumStations);

    for(i=0;i<NumStations;i++)
    {
        MyLine = StationFile.readLine().simplified();
        MyList = MyLine.split(";");
        TempString = MyList.value(0);
        NOAAStations[0][i] = TempString.toDouble();
        TempString = MyList.value(1);
        NOAAStations[1][i] = TempString.toDouble();
        TempString = MyList.value(2);
        NOAAStations[2][i] = TempString.toDouble();
        NOAAStationNames[i] = MyList.value(3);
    }
    StationFile.close();
    drawMarkers(true);
    return;
}

//Routine that formats the response from the NOAA server in CSV
QString MainWindow::FormatNOAAResponse(QByteArray Input)
{
    int i;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QString InputData(Input);
    QStringList DataList = InputData.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    QStringList TimeSnap;

    Output = "'";
    CurrentStation.resize(DataList.length()-1);
    for(i=1;i<DataList.length();i++)
    {
        TempData = DataList.value(i);
        TimeSnap = TempData.split(",");
        DateS    = TimeSnap.value(0);
        YearS    = DateS.mid(0,4);
        MonthS   = DateS.mid(5,2);
        DayS     = DateS.mid(8,2);
        HourMinS = DateS.mid(11,5);
        HourS    = HourMinS.mid(0,2);
        MinS     = HourMinS.mid(3,2);
        WLS      = TimeSnap.value(1);
        Output=Output+YearS+":"+MonthS+":"+
               DayS+":"+HourS+":"+MinS+":"+WLS+";";
        TempData = YearS+"/"+MonthS+"/"+DayS;
        CurrentStation[i-1].Date.setDate(YearS.toInt(),MonthS.toInt(),DayS.toInt());
        CurrentStation[i-1].Time = QTime(HourS.toInt(),MinS.toInt(),0);
        CurrentStation[i-1].value = WLS.toDouble();
    }
    Output = Output+"'";

    return Output;
}

//Initializes the locations for the pan to box
void MainWindow::initializePanToLocations(int size)
{
    panToLocations.resize(size);
    //Locations in the select box to pan to
    panToLocations[0]  = "-89.33,24.73,6";  //Gulf of Mexico
    panToLocations[1]  = "-72.45,36.05,5";  //Atlantic Ocean
    panToLocations[2]  = "-141.24,40.46,4"; //Pacific Ocean
    panToLocations[3]  = "0,0,0";           //-----------------
    panToLocations[4]  = "-87.49,30.71,9";  //Alabama
    panToLocations[5]  = "-152.75,64.00,4"; //Alaska
    panToLocations[6]  = "-121.10,37.36,6"; //California
    panToLocations[7]  = "-72.82,41.43,9";  //Connecticut
    panToLocations[8]  = "-75.08,39.11,9";  //Deleware
    panToLocations[9]  = "-77.02,38.88,13"; //Washington, DC
    panToLocations[10] = "-83.77,28.08,13"; //Florida
    panToLocations[11] = "-80.67,31.67,8";  //Georgia
    panToLocations[12] = "-157.40,20.67,7"; //Hawaii
    panToLocations[13] = "-91.5,29.5,8";    //Louisiana
    panToLocations[14] = "-68.69,44.07,8";  //Maine
    panToLocations[15] = "-76.18,38.41,8";  //Maryland
    panToLocations[16] = "-70.73,42.12,9";  //Massachusettes
    panToLocations[17] = "-87.95,30.46,10"; //Mississippi
    panToLocations[18] = "-70.73,42.98,11"; //New Hampshire
    panToLocations[19] = "-73.43,40.15,8";  //New Jersey
    panToLocations[20] = "-73.16,41.11,9";  //New York
    panToLocations[21] = "-76.79,32.25,8";  //North Carolina
    panToLocations[22] = "-122.99,44.41,7"; //Oregon
    panToLocations[23] = "-66.43,18.22,9";  //Puerto Rico
    panToLocations[24] = "-71.25,41.58,10"; //Rhode Island
    panToLocations[25] = "-79.67,33.21,8";  //South Carolina
    panToLocations[26] = "-94.72,28.55,7";  //Texas
    panToLocations[27] = "-121.81,47.55,7"; //Washington
    panToLocations[28] = "-76.29,37.85,8";  //Virginia
    return;
}

QString MainWindow::retrieveProduct(int type)
{
    QString Product;
    int index = ui->combo_NOAAProduct->currentIndex();
    if(type==1)
    {
        switch(index)
        {
            case(0):Product = "Observed Water Level"; break;
            case(1):Product = "Predicted Water Level"; break;
            case(2):Product = "Current Speed"; break;
            case(3):Product = "Air Temperature"; break;
            case(4):Product = "Water Temperature"; break;
            case(5):Product = "Wind Speed"; break;
            case(6):Product = "Relative Humidity"; break;
            case(7):Product = "Air Pressure"; break;
        }
    }
    else if(type==2)
    {
        switch(index)
        {
            case(0):Product = "water_level"; break;
            case(1):Product = "predictions"; break;
            case(2):Product = "currents"; break;
            case(3):Product = "air_temperature"; break;
            case(4):Product = "water_temperature"; break;
            case(5):Product = "wind"; break;
            case(6):Product = "humidity"; break;
            case(7):Product = "air_pressure"; break;
        }
    }
    return Product;
}
