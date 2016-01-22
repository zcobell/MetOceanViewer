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

#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries.h>

QVector< QVector<QByteArray> > NOAAWebData;

//-------------------------------------------//
//Routine to read the data that has been
//received from the NOAA server
//-------------------------------------------//
void MainWindow::ReadNOAAResponse(QNetworkReply *reply, int index, int index2)
{
    QByteArray Data;

    //Catch some errors during the download
    if(reply->error()!=0)
    {
        QMessageBox::information(this,"ERROR","ERROR: "+reply->errorString());
        ui->statusBar->clearMessage();
        reply->deleteLater();
        return;
    }

    //Read the data received from NOAA server
    Data=reply->readAll();

    //Save the data into an array and increment the counter
    NOAAWebData[index2][index] = Data;

    //Delete this response
    reply->deleteLater();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Routine that fires after the NOAA data is
//finished downloading
//-------------------------------------------//
void MainWindow::PlotNOAAResponse()
{
    QVector<QString> NOAAData,Error;
    QString javascript;
    int i;

    for(i=0;i<CurrentNOAAStation.length();i++)
        CurrentNOAAStation[i].clear();
    CurrentNOAAStation.clear();

    NOAAData.resize(NOAAWebData.length());
    Error.resize(NOAAWebData.length());
    for(i=0;i<NOAAWebData.length();i++)
    {
        NOAAData[i] = FormatNOAAResponse(NOAAWebData[i],Error[i],i);
        Error[i].remove(QRegExp("[\\n\\t\\r]"));
        javascript="AddDataSeries("+QString::number(i)+","+NOAAData[i]+",'"+Error[i]+"')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("PlotTimeseries()");

    ui->statusBar->clearMessage();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Routine that formats the response from the
//NOAA server in CSV
//-------------------------------------------//
QString MainWindow::FormatNOAAResponse(QVector<QByteArray> Input,QString &ErrorString,int index)
{
    int i,j,k;
    int dataCount;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QStringList TimeSnap;
    QVector<QString> InputData;
    QVector<QStringList> DataList;
    QVector<QString> Temp;

    InputData.resize(Input.length());
    DataList.resize(Input.length());
    Temp.resize(Input.length());

    for(i=0;i<DataList.length();i++)
    {
        InputData[i] = QString(Input[i]);
        DataList[i] = InputData[i].split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
        Temp[i] = QString(Input[i]);
        ErrorString = Temp[i] + "\n";
    }

    Output = "'";

    dataCount = 0;
    for(i=0;i<DataList.length();i++)
        dataCount = dataCount+DataList[i].length()-1;

    CurrentNOAAStation[index].resize(dataCount);

    k = 0;
    for(j=0;j<DataList.length();j++)
    {
        for(i=1;i<DataList[j].length();i++)
        {
            TempData = DataList[j].value(i);
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
            CurrentNOAAStation[index][k].Date.setDate(YearS.toInt(),MonthS.toInt(),DayS.toInt());
            CurrentNOAAStation[index][k].Time = QTime(HourS.toInt(),MinS.toInt(),0);
            CurrentNOAAStation[index][k].value = WLS.toDouble();
            k = k + 1;
        }
    }
    Output = Output+"'";

    return Output;
}
//-------------------------------------------//


//-------------------------------------------//
//Function that picks the product name and
//the product id on the NOAA site by the
//index in the NOAA selection combo box
//-------------------------------------------//
int MainWindow::retrieveProduct(int type, QString &Product, QString &Product2)
{
    Product2 = "null";
    int index = ui->combo_NOAAProduct->currentIndex();
    if(type==1)
    {
        switch(index)
        {
            case(0):Product = "6 Min Observed Water Level vs. Predicted"; break;
            case(1):Product = "6 Min Observed Water Level"; break;
            case(2):Product = "Hourly Observed Water Level"; break;
            case(3):Product = "Predicted Water Level"; break;
            case(4):Product = "Air Temperature"; break;
            case(5):Product = "Water Temperature"; break;
            case(6):Product = "Wind Speed"; break;
            case(7):Product = "Relative Humidity"; break;
            case(8):Product = "Air Pressure"; break;
        }
    }
    else if(type==2)
    {
        switch(index)
        {
            case(0):Product = "water_level"; Product2 = "predictions"; break;
            case(1):Product = "water_level"; break;
            case(2):Product = "hourly_height"; break;
            case(3):Product = "predictions"; break;
            case(4):Product = "air_temperature"; break;
            case(5):Product = "water_temperature"; break;
            case(6):Product = "wind"; break;
            case(7):Product = "humidity"; break;
            case(8):Product = "air_pressure"; break;
        }
    }
    return 0;
}
//-------------------------------------------//
