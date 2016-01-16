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

#include "MetOceanViewer.h"
#include "ui_MetOceanViewer_main.h"

//-------------------------------------------//
//Function that fires on the signal when the
//USGS data has finished downloading
//-------------------------------------------//
void MainWindow::ReadUSGSDataFinished(QNetworkReply *reply)
{
    if(reply->error()!=0)
    {
        QMessageBox::information(this,"ERROR","ERROR: "+reply->errorString());
        ui->statusBar->clearMessage();
        return;
    }

    //Read the data that was received
    QByteArray RawUSGSData = reply->readAll();

    //Put the data into an array with all variables
    if(USGSdataMethod==0||USGSdataMethod==1)
        FormatUSGSInstantResponse(RawUSGSData);
    else
        FormatUSGSDailyResponse(RawUSGSData);

    //Delete the QNetworkReply object off the heap
    reply->deleteLater();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Formats the USGS daily response when
//the download has completed.
//-------------------------------------------//
void MainWindow::FormatUSGSDailyResponse(QByteArray Input)
{
    int i,j,ParamStart,ParamStop;
    int HeaderEnd;
    double TempData;
    QStringList TempList;
    QString TempLine,TempDateString;
    QString InputData(Input);
    QStringList SplitByLine = InputData.split(QRegExp("[\n]"),QString::SkipEmptyParts);
    QVector<QString> Parameters;
    QDateTime CurrentDate;
    bool doubleok;

    ParamStart = -1;
    ParamStop = -1;
    HeaderEnd = -1;

    //Save the potential error string
    USGSErrorString = InputData.remove(QRegExp("[\n\t\r]"));

    //Start by finding the header and reading the parameters from it
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(15)=="# Data provided")
        {
            ParamStart = i + 2;
            break;
        }
    }

    for(i=ParamStart;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        if(TempLine == "#")
        {
            ParamStop = i - 1;
            break;
        }
    }

    Parameters.resize(ParamStop-ParamStart+1);

    for(i=ParamStart;i<=ParamStop;i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(" ",QString::SkipEmptyParts);
        Parameters[i-ParamStart] = QString();
        for(j=3;j<TempList.length();j++)
        {
            if(j==3)
                Parameters[i-ParamStart] = TempList.value(j);
            else
                Parameters[i-ParamStart] = Parameters[i-ParamStart] + " " + TempList.value(j);
        }

    }

    //Add the parameters to the selection box
    for(i=0;i<Parameters.length();i++)
    {
        Parameters[i] = Parameters[i].mid(5);
        ui->combo_USGSProduct->addItem(Parameters[i]);
    }

    //Find out where the header ends
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(1) != "#")
        {
            HeaderEnd = i + 2;
            break;
        }
    }

    //Delete the old data
    for(i=0;i<CurrentUSGSStation.length();i++)
    {
        CurrentUSGSStation[i].Data.clear();
        CurrentUSGSStation[i].Date.clear();
        CurrentUSGSStation[i].NumDataPoints = 0;
    }
    CurrentUSGSStation.clear();

    //Initialize the array
    CurrentUSGSStation.resize(Parameters.length());

    //Read the data into the array
    for(i=HeaderEnd;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(QRegExp("[\t]"));
        TempDateString = TempList.value(2);
        CurrentDate = QDateTime::fromString(TempDateString,"yyyy-MM-dd");
        CurrentDate.setTimeSpec(Qt::UTC);
        for(j=0;j<Parameters.length();j++)
        {
            TempData = TempList.value(2*j+3).toDouble(&doubleok);
            if(!TempList.value(2*j+3).isNull() && doubleok)
            {
                CurrentUSGSStation[j].NumDataPoints = CurrentUSGSStation[j].NumDataPoints + 1;
                CurrentUSGSStation[j].Data.resize(CurrentUSGSStation[j].Data.length()+1);
                CurrentUSGSStation[j].Date.resize(CurrentUSGSStation[j].Date.length()+1);
                CurrentUSGSStation[j].Data[CurrentUSGSStation[j].Data.length()-1] = TempData;
                CurrentUSGSStation[j].Date[CurrentUSGSStation[j].Date.length()-1] = CurrentDate;
            }
        }
    }

    USGSdataReady = true;
    PlotUSGS();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Formats the USGS instant data respone when
//the data has finished downloading
//-------------------------------------------//
void MainWindow::FormatUSGSInstantResponse(QByteArray Input)
{
    int i,j,ParamStart,ParamStop,OffsetHours;
    int HeaderEnd;
    double TempData;
    QStringList TempList;
    QString TempLine,TempDateString,TempTimeZoneString,TempString;
    QString InputData(Input);
    QStringList SplitByLine = InputData.split(QRegExp("[\n]"),QString::SkipEmptyParts);
    QVector<QString> Parameters;
    QDateTime CurrentDate;
    bool doubleok;

    ParamStart = -1;
    ParamStop = -1;
    HeaderEnd = -1;

    if(InputData.isEmpty()||InputData.isNull())
    {
        QMessageBox::critical(this,"ERROR","This data is not available except from the USGS archive server.");
        return;
    }

    //Save the potential error string
    USGSErrorString = InputData.remove(QRegExp("[\n\t\r]"));
    USGSErrorString = "";

    //Start by finding the header and reading the parameters from it
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(15)=="# Data provided")
        {
            ParamStart = i + 2;
            break;
        }
    }

    for(i=ParamStart;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        if(TempLine == "#")
        {
            ParamStop = i - 1;
            break;
        }
    }

    Parameters.resize(ParamStop-ParamStart+1);

    for(i=ParamStart;i<=ParamStop;i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(" ",QString::SkipEmptyParts);
        Parameters[i-ParamStart] = QString();
        for(j=3;j<TempList.length();j++)
        {
            if(j==3)
                Parameters[i-ParamStart] = TempList.value(j);
            else
                Parameters[i-ParamStart] = Parameters[i-ParamStart] + " " + TempList.value(j);
        }

    }

    //Add the parameters to the selection box
    for(i=0;i<Parameters.length();i++)
        ui->combo_USGSProduct->addItem(Parameters[i]);

    //Find out where the header ends
    for(i=0;i<SplitByLine.length();i++)
    {
        if(SplitByLine.value(i).left(1) != "#")
        {
            HeaderEnd = i + 2;
            break;
        }
    }

    //Delete the old data
    for(i=0;i<CurrentUSGSStation.length();i++)
    {
        CurrentUSGSStation[i].Data.clear();
        CurrentUSGSStation[i].Date.clear();
        CurrentUSGSStation[i].NumDataPoints = 0;
    }
    CurrentUSGSStation.clear();

    //Initialize the array
    CurrentUSGSStation.resize(Parameters.length());

    //Read the data into the array
    for(i=HeaderEnd;i<SplitByLine.length();i++)
    {
        TempLine = SplitByLine.value(i);
        TempList = TempLine.split(QRegExp("[\t]"));
        TempDateString = TempList.value(2);
        TempTimeZoneString = TempList.value(3);
        CurrentDate = QDateTime::fromString(TempDateString,"yyyy-MM-dd hh:mm");
        CurrentDate.setTimeSpec(Qt::UTC);
        OffsetHours = GetTimezoneOffset(TempTimeZoneString);
        CurrentDate = CurrentDate.addSecs(3600*OffsetHours);
        for(j=0;j<Parameters.length();j++)
        {
            TempData = TempList.value(2*j+4).toDouble(&doubleok);
            if(!TempList.value(2*j+4).isNull() && doubleok)
            {
                CurrentUSGSStation[j].NumDataPoints = CurrentUSGSStation[j].NumDataPoints + 1;
                CurrentUSGSStation[j].Data.resize(CurrentUSGSStation[j].Data.length()+1);
                CurrentUSGSStation[j].Date.resize(CurrentUSGSStation[j].Date.length()+1);
                CurrentUSGSStation[j].Data[CurrentUSGSStation[j].Data.length()-1] = TempData;
                CurrentUSGSStation[j].Date[CurrentUSGSStation[j].Date.length()-1] = CurrentDate;
            }
        }
    }

    USGSdataReady = true;
    PlotUSGS();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Plots the USGS data and sends everything
//to the HTML/Javascript backend
//-------------------------------------------//
void MainWindow::PlotUSGS()
{
    QString PlotData,javastring,Product;

    ui->statusBar->showMessage("Plotting USGS data...");

    PlotData="'";

    //Get the index of the data to plot
    int index = ui->combo_USGSProduct->currentIndex();

    //Put the data into a plotting object
    USGSPlot.resize(CurrentUSGSStation[index].Date.length());
    for(int i=0;i<CurrentUSGSStation[index].Date.length();i++)
    {
        USGSPlot[i].Date = CurrentUSGSStation[index].Date[i].date();
        USGSPlot[i].Time = CurrentUSGSStation[index].Date[i].time();
        USGSPlot[i].value = CurrentUSGSStation[index].Data[i];
        PlotData=PlotData+USGSPlot[i].Date.toString("yyyy:MM:dd")+":"+
                USGSPlot[i].Time.toString("hh:mm")+":"+QString::number(USGSPlot[i].value)+";";
    }
    PlotData = PlotData+"'";

    Product = ui->combo_USGSProduct->currentText();
    javastring="drawUSGSData("+PlotData+",'"+Product+"','"+Product.split(",").value(0)+"',"+
            QString::number(CurrentUSGSStation[index].NumDataPoints)+",'"+USGSErrorString+"')";
    ui->usgs_map->page()->mainFrame()->evaluateJavaScript(javastring);

    USGSbeenPlotted = true;

    ui->statusBar->clearMessage();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//This routine will help convert everything back to UTC since
//USGS supplies all products in local time via the web api
//-------------------------------------------//
int MainWindow::GetTimezoneOffset(QString timezone)
{

    if(timezone.isNull()||timezone.isEmpty())
        return 0;
    else if(timezone=="UTC")
        return 0;
    else if(timezone=="GMT")
        return 0;
    else if(timezone=="EST")
        return -5;
    else if(timezone=="EDT")
        return -4;
    else if(timezone=="CST")
        return -6;
    else if(timezone=="CDT")
        return -5;
    else if(timezone=="MST")
        return -7;
    else if(timezone=="MDT")
        return -6;
    else if(timezone=="PST")
        return -8;
    else if(timezone=="PDT")
        return -7;
    else if(timezone=="AKST")
        return -9;
    else if(timezone=="AKDT")
        return -8;
    else if(timezone=="HST")
        return -10;
    else if(timezone=="HDT")
        return -9;
    else if(timezone=="AST")
        return -4;
    else if(timezone=="ADT")
        return -3;
    else if(timezone=="SST")
        return -11;
    else if(timezone=="SDT")
        return -10;
    else
    {
        //qDebug() << timezone;
        QMessageBox::warning(this,"WARNING","The timezone conversion is not known for this location. The data is not converted to UTC.");
        return 0;
    }
}
//-------------------------------------------//


//-------------------------------------------//
//Function to save the map and chart as a jpg
//-------------------------------------------//
void MainWindow::on_button_usgssavemap_clicked()
{
    if(USGSMarkerID=="none")
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    QVariant USGSMarkerID2 = ui->usgs_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    if(USGSMarkerID != USGSMarkerID2.toString().split(";").value(0).mid(4))
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }
    if(!USGSbeenPlotted)
    {
        QMessageBox::critical(this,"ERROR","Plot the data before attempting to save.");
        return;
    }

    QString filter = "JPG (*.jpg *.jpeg)";
    QString DefaultFile = "/USGS_"+USGSMarkerID+".jpg";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile USGSOutput(Filename);
    QPixmap USGSImage(ui->usgs_map->size());
    ui->usgs_map->render(&USGSImage);
    USGSOutput.open(QIODevice::WriteOnly);
    USGSImage.save(&USGSOutput,"JPG",100);
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Saves the USGS data as an IMEDS formatted file
//or a CSV
//-------------------------------------------//
void MainWindow::on_button_usgssavedata_clicked()
{
    if(USGSMarkerID=="none")
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    QVariant USGSMarkerID2 = ui->usgs_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    if(USGSMarkerID != USGSMarkerID2.toString().split(";").value(0).mid(4))
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }
    if(!USGSbeenPlotted)
    {
        QMessageBox::critical(this,"ERROR","Plot the data before attempting to save.");
        return;
    }

    QString filter;
    QString DefaultFile = "/USGS_"+USGSMarkerID+".imeds";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory+DefaultFile,"IMEDS (*.imeds);;CSV (*.csv)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(Filename == NULL)
        return;

    GetLeadingPath(Filename);
    QFile USGSOutput(Filename);
    QTextStream Output(&USGSOutput);
    USGSOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+USGSMarkerID+"\n";
        Output << "Datum: N/A\n";
        Output << "Units: N/A\n";
        Output << "\n";
        for(int i=0;i<USGSPlot.length();i++)
        {
            Output << USGSPlot[i].Date.toString("MM/dd/yyyy")+","+
                      USGSPlot[i].Time.toString("hh:mm")+","+
                      QString::number(USGSPlot[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec value\n";
        Output << "USGS   UTC    N/A\n";
        Output << "USGS_"+USGSMarkerID+"   "+QString::number(CurrentUSGSLat)+
                  "   "+QString::number(CurrentUSGSLon)+"\n";
        for(int i=0;i<USGSPlot.length();i++)
        {
            Output << USGSPlot[i].Date.toString("yyyy")+"    "+
                        USGSPlot[i].Date.toString("MM")+"    "+
                        USGSPlot[i].Date.toString("dd")+"    "+
                        USGSPlot[i].Time.toString("hh")+"    "+
                        USGSPlot[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(USGSPlot[i].value)+"\n";
        }

    }
    USGSOutput.close();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Fires when the combo box is changed and
//plots the data immediately
//-------------------------------------------//
void MainWindow::on_combo_USGSProduct_currentIndexChanged(int index)
{
    if(USGSdataReady)
        PlotUSGS();
    return;
}
//-------------------------------------------//
