//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries.h>

//-------------------------------------------//
//Routine that fires after the NOAA data is
//read from the server
//-------------------------------------------//
void MainWindow::ReadNOAADataFinished(QNetworkReply *reply)
{
    QByteArray NOAAWebData;
    QString NOAAData,javastring,Error;
    QString Datum,Units,Product;
    int ProductIndex;

    Units = ui->combo_noaaunits->currentText();
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    Product = retrieveProduct(1);

    if(ProductIndex == 0 || ProductIndex == 1 || ProductIndex == 2)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        Datum = ui->combo_noaadatum->currentText();
    }
    else if(ProductIndex == 5)
    {
        if(Units=="metric")
            Units="m/s";
        else
            Units="knots";
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
        ui->statusBar->clearMessage();
        return;
    }
    NOAAWebData=reply->readAll();
    NOAAData = FormatNOAAResponse(NOAAWebData,Error);
    Error.remove(QRegExp("[\\n\\t\\r]"));

    javastring="drawNOAAData("+NOAAData+",'"+Datum+"','"+Units+"','"+Product+"','"+Error+"')";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);

    reply->deleteLater();
    ui->statusBar->clearMessage();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Routine that formats the response from the
//NOAA server in CSV
//-------------------------------------------//
QString MainWindow::FormatNOAAResponse(QByteArray Input,QString &ErrorString)
{
    int i;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QString InputData(Input);
    QStringList DataList = InputData.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    QStringList TimeSnap;
    QString Temp(Input);
    ErrorString = Temp;

    Output = "'";
    CurrentNOAAStation.resize(DataList.length()-1);
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
        CurrentNOAAStation[i-1].Date.setDate(YearS.toInt(),MonthS.toInt(),DayS.toInt());
        CurrentNOAAStation[i-1].Time = QTime(HourS.toInt(),MinS.toInt(),0);
        CurrentNOAAStation[i-1].value = WLS.toDouble();
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
QString MainWindow::retrieveProduct(int type)
{
    QString Product;
    int index = ui->combo_NOAAProduct->currentIndex();
    if(type==1)
    {
        switch(index)
        {
            case(0):Product = "6 Min Observed Water Level"; break;
            case(1):Product = "Hourly Observed Water Level"; break;
            case(2):Product = "Predicted Water Level"; break;
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
            case(1):Product = "hourly_height"; break;
            case(2):Product = "predictions"; break;
            case(3):Product = "air_temperature"; break;
            case(4):Product = "water_temperature"; break;
            case(5):Product = "wind"; break;
            case(6):Product = "humidity"; break;
            case(7):Product = "air_pressure"; break;
        }
    }
    return Product;
}
//-------------------------------------------//
