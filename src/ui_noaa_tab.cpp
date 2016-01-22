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
//Called when the pan to combo box is updated
//-------------------------------------------//
void MainWindow::on_Combo_NOAAPanTo_currentIndexChanged(int index)
{
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("panTo('"+ui->Combo_NOAAPanTo->currentText()+"')");
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user attempts to save the NOAA chart
//-------------------------------------------//
void MainWindow::on_button_noaasavechart_clicked()
{

    if(NOAAMarkerID==-1)
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }
    QVariant eval = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");
    int NOAAMarkerID2 = evalList.value(0).toInt();
    if(NOAAMarkerID != NOAAMarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter = "JPG (*.jpg *.jpeg)";
    QString DefaultFile = "/NOAA_"+QString::number(NOAAMarkerID)+".jpg";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile NOAAOutput(Filename);
    QPixmap NOAAImage(ui->noaa_map->size());
    ui->noaa_map->render(&NOAAImage);
    NOAAOutput.open(QIODevice::WriteOnly);
    NOAAImage.save(&NOAAOutput,"JPG",100);

}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user tries to save the NOAA data
//-------------------------------------------//
void MainWindow::on_button_noaasavedata_clicked()
{

    if(NOAAMarkerID==-1)
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }
    QVariant eval = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");
    int NOAAMarkerID2 = evalList.value(0).toInt();
    if(NOAAMarkerID != NOAAMarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter;
    QString DefaultFile = "/NOAA_"+QString::number(NOAAMarkerID)+".imeds";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory+DefaultFile,
                                                    "IMEDS (*.imeds);;CSV (*.csv)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(Filename == NULL)
        return;

    GetLeadingPath(Filename);
    QFile NOAAOutput(Filename);
    QTextStream Output(&NOAAOutput);
    NOAAOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+QString::number(NOAAMarkerID)+"\n";
        Output << "Datum: "+ui->combo_noaadatum->currentText()+"\n";
        Output << "Units: "+ui->combo_noaaunits->currentText()+"\n";
        Output << "\n";
        for(int i=0;i<CurrentNOAAStation.length();i++)
        {
            Output << CurrentNOAAStation[i].Date.toString("MM/dd/yyyy")+","+
                      CurrentNOAAStation[i].Time.toString("hh:mm")+","+
                      QString::number(CurrentNOAAStation[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        QString datum = ui->combo_noaadatum->currentText();
        QString units = ui->combo_noaaunits->currentText();
        QString units2;
        if(units.compare("metric")==0)
        {
            units = "meters";
            units2 = "m";
        }
        else
        {
            units = "feet";
            units2 = "ft";
        }
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec watlev("+units2+")\n";
        Output << "NOAA    UTC    "+datum+"\n";
        Output << "NOAA_"+QString::number(NOAAMarkerID)+"   "+QString::number(CurrentNOAALat)+
                  "   "+QString::number(CurrentNOAALon)+"\n";
        for(int i=0;i<CurrentNOAAStation.length();i++)
        {
            Output << CurrentNOAAStation[i].Date.toString("yyyy")+"    "+
                        CurrentNOAAStation[i].Date.toString("MM")+"    "+
                        CurrentNOAAStation[i].Date.toString("dd")+"    "+
                        CurrentNOAAStation[i].Time.toString("hh")+"    "+
                        CurrentNOAAStation[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(CurrentNOAAStation[i].value)+"\n";
        }

    }
    NOAAOutput.close();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the NOAA fetch data button is clicked
//-------------------------------------------//
void MainWindow::on_Button_FetchData_clicked()
{
    QEventLoop loop;
    qint64 Duration;
    QString PlotTitle,RequestURL,StartString,EndString,Product,Product2;
    QString javascript,Units,Datum,XLabel,YLabel;
    QDateTime StartDate,EndDate;
    int i,j,ierr,ProductIndex,NumDownloads,NumData;
    QVector<QDateTime> StartDateList,EndDateList;

    //Display the wait cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QVariant eval = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");

    NOAAMarkerID = evalList.value(0).toInt();
    CurrentNOAAStationName = evalList.value(1).simplified();
    CurrentNOAALat = evalList.value(3).toDouble();
    CurrentNOAALon = evalList.value(2).toDouble();

    //Update status
    ui->statusBar->showMessage("Downloading data from NOAA...",0);

    //Get the date range
    StartDate = ui->Date_StartTime->dateTime();
    EndDate = ui->Date_EndTime->dateTime();

    if(StartDate==EndDate||EndDate<StartDate)
    {
        QMessageBox::critical(this,"ERROR","Dates must be a valid range.");
        ui->statusBar->clearMessage();
        QApplication::restoreOverrideCursor();
        return;
    }

    //Begin organizing the dates for download
    Duration = StartDate.daysTo(EndDate);
    NumDownloads = (Duration / 30) + 1;
    StartDateList.resize(NumDownloads);
    EndDateList.resize(NumDownloads);

    //Build the list of dates in 30 day intervals
    for(i=0;i<NumDownloads;i++)
    {
        StartDateList[i] = StartDate.addDays(i*30).addDays(i);
        StartDateList[i].setTime(QTime(0,0,0));
        EndDateList[i]   = StartDateList[i].addDays(30);
        EndDateList[i].setTime(QTime(23,59,59));
        if(EndDateList[i]>EndDate)
            EndDateList[i] = EndDate;
    }

    //Get options
    Units = ui->combo_noaaunits->currentText();
    ierr = retrieveProduct(2,Product,Product2);
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    if(ProductIndex == 0 || ProductIndex == 1 || ProductIndex == 2 || ProductIndex == 3)
        Datum = ui->combo_noaadatum->currentText();
    else if(ProductIndex == 4 || ProductIndex == 5
         || ProductIndex == 6 || ProductIndex == 7 || ProductIndex == 8)
        Datum = "Stnd";

    if(ProductIndex==0)
        NumData = 2;
    else
        NumData = 1;

    //Allocate the NOAA array
    NOAAWebData.clear();
    NOAAWebData.resize(NumData);
    for(i=0;i<NumData;i++)
        NOAAWebData[i].resize(NumDownloads);

    for(j=0;j<NumData;j++)
    {
        for(i=0;i<NumDownloads;i++)
        {
            //Make the date string
            StartString = StartDateList[i].toString("yyyyMMdd hh:mm");
            EndString = EndDateList[i].toString("yyyyMMdd hh:mm");

            //Build the URL to request data from the NOAA CO-OPS API
            if(j==0)
                RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                             QString("product="+Product+"&application=metoceanviewer")+
                             QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                             QString("&station=")+QString::number(NOAAMarkerID)+
                             QString("&time_zone=GMT&units=")+Units+
                             QString("&interval=&format=csv");
            else
                RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                             QString("product="+Product2+"&application=metoceanviewer")+
                             QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                             QString("&station=")+QString::number(NOAAMarkerID)+
                             QString("&time_zone=GMT&units=")+Units+
                             QString("&interval=&format=csv");

            //Allow a different datum where allowed
            if(Datum != "Stnd")RequestURL = RequestURL+QString("&datum=")+Datum;

            //Send the request
            QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(RequestURL)));
            connect(reply,SIGNAL(finished()),&loop,SLOT(quit()));
            connect(reply,SIGNAL(error(QNetworkReply::NetworkError)),&loop,SLOT(quit()));
            loop.exec();
            ReadNOAAResponse(reply,i,j);

        }
    }

    //Update status
    ui->statusBar->showMessage("Plotting the data from NOAA...",0);

    Units = ui->combo_noaaunits->currentText();
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    ierr = retrieveProduct(1,Product,Product2);

    if(ProductIndex == 0)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        Datum = ui->combo_noaadatum->currentText();
        YLabel = "Water Level ("+Units+", "+Datum+")";
    }
    else if(ProductIndex == 1 || ProductIndex == 2 || ProductIndex == 3)
    {
        if(Units=="metric")
            Units="m";
        else
            Units="ft";
        Datum = ui->combo_noaadatum->currentText();
        YLabel = Product+" ("+Units+", "+Datum+")";
    }
    else if(ProductIndex == 6)
    {
        if(Units=="metric")
            Units="m/s";
        else
            Units="knots";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }
    else if(ProductIndex == 4 || ProductIndex == 5)
    {
        if(Units=="metric")
            Units="Celcius";
        else
            Units="Fahrenheit";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }
    else if(ProductIndex == 7)
    {
        Units = "%";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }
    else if(ProductIndex == 8)
    {
        Units = "mb";
        Datum = "Stnd";
        YLabel = Product+" ("+Units+")";
    }

    XLabel = "Date";
    PlotTitle = "Station "+QString::number(NOAAMarkerID)+": "+CurrentNOAAStationName;
    javascript = "setGlobal('"+PlotTitle+"','"+XLabel+"','"+YLabel+"','"+QString::number(FLAG_NULL_TS)+"')";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);

    if(NumData==2)
    {
        javascript = "SetSeriesOptions(0,'Observed Water Level','#0101DF')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
        javascript = "SetSeriesOptions(1,'Predicted Water Level','#00FF00')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }
    else
    {
        javascript = "SetSeriesOptions(0,'"+Product+"','#0101DF')";
        ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    javascript = "allocateData("+QString::number(NumData)+")";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javascript);

    //Call the plotting routine
    PlotNOAAResponse();

    //Remove the wait cursor
    QApplication::restoreOverrideCursor();
    ui->statusBar->clearMessage();


    return;
}
//-------------------------------------------//

