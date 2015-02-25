//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2015.
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
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: ui_noaa_tab.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"

//Called when the pan to combo box is updated
void MainWindow::on_Combo_PanTo_currentIndexChanged(int index)
{
    //Call the javascript code to reposition the google map center and zoom
    double x,y,z;
    QString data,xS,yS,zS;
    data = panToLocations.value(index);
    QStringList List = data.split(",");
    xS = List.value(0);
    yS = List.value(1);
    zS = List.value(2);
    x = xS.toDouble();
    y = yS.toDouble();
    z = zS.toDouble();
    QString javastring = "panTo("+QString::number(x)+","+QString::number(y)+","+QString::number(z)+")";
    ui->noaa_map->page()->mainFrame()->evaluateJavaScript(javastring);
}

//Called when the user attempts to save the NOAA chart
void MainWindow::on_button_savechart_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile NOAAOutput(Filename);
    QPixmap NOAAImage(ui->noaa_map->size());
    ui->noaa_map->render(&NOAAImage);
    NOAAOutput.open(QIODevice::WriteOnly);
    NOAAImage.save(&NOAAOutput,"JPG",100);

}

//Called when the user tries to save the NOAA data
void MainWindow::on_button_savedata_clicked()
{

    if(MarkerID==-1)
    {
        QMessageBox::information(this,"ERROR","No Station has been selected.");
        return;
    }
    QVariant MarkerID2 = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    if(MarkerID != MarkerID2)
    {
        QMessageBox::information(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter;
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory,"CSV (*.csv);;IMEDS (*.imeds)",&filter);
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
        Output << "Station: "+MarkerID.toString()+"\n";
        Output << "Datum: "+ui->combo_noaadatum->currentText()+"\n";
        Output << "Units: "+ui->combo_noaaunits->currentText()+"\n";
        Output << "\n";
        for(int i=0;i<CurrentStation.size();i++)
        {
            Output << CurrentStation[i].Date.toString("MM/dd/yyyy")+","+
                      CurrentStation[i].Time.toString("hh:mm")+","+
                      QString::number(CurrentStation[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        //Find the station index from the station ID
        double Lat,Lon;
        Lat = -9999;
        Lon = -9999;
        for(int i=0;i<NOAAStations[0].size();i++)
        {
            if(MarkerID.toDouble()==NOAAStations[0][i])
            {
                Lat = NOAAStations[2][i];
                Lon = NOAAStations[1][i];
            }
        }

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
        Output << "NOAA_"+MarkerID.toString()+"   "+QString::number(Lat)+"   "+QString::number(Lon)+"\n";
        for(int i=0;i<CurrentStation.size();i++)
        {
            Output << CurrentStation[i].Date.toString("yyyy")+"    "+
                        CurrentStation[i].Date.toString("MM")+"    "+
                        CurrentStation[i].Date.toString("dd")+"    "+
                        CurrentStation[i].Time.toString("hh")+"    "+
                        CurrentStation[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(CurrentStation[i].value)+"\n";
        }

    }
    NOAAOutput.close();

}

//Called when the NOAA fetch data button is clicked
void MainWindow::on_Button_FetchData_clicked()
{
    qint64 Duration;
    QString RequestURL,StartString,EndString,Datum,Units,Product;
    QDateTime StartDate,EndDate;
    int ProductIndex;
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    MarkerID = ui->noaa_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");

    //Get the date range
    StartDate = ui->Date_StartTime->dateTime();
    EndDate = ui->Date_EndTime->dateTime();

    //Sanity check
    Duration = StartDate.daysTo(EndDate);
    if(Duration>31)
    {
        QMessageBox::information(NULL,"ERROR","Date range must be less than 31 days.");
        return;
    }
    if(StartDate==EndDate||EndDate<StartDate)
    {
        QMessageBox::information(NULL,"ERROR","Dates must be a valid range.");
        return;
    }

    //Get options
    Units = ui->combo_noaaunits->currentText();
    Product = retrieveProduct(2);
    ProductIndex = ui->combo_NOAAProduct->currentIndex();
    if(ProductIndex == 0 || ProductIndex == 1 || ProductIndex == 2)
        Datum = ui->combo_noaadatum->currentText();
    else if(ProductIndex == 3 || ProductIndex == 5
         || ProductIndex == 4 || ProductIndex == 6 || ProductIndex == 7)
        Datum = "Stnd";


    //Make the date string
    StartString = StartDate.toString("yyyyMMdd");
    EndString = EndDate.toString("yyyyMMdd");
    StartString = StartString+" 00:00";
    EndString = EndString+" 23:59";

    //Connect the finished downloading signal to the routine that plots the markers
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(ReadNOAADataFinished(QNetworkReply*)));
    RequestURL = QString("http://tidesandcurrents.noaa.gov/api/datagetter?")+
                 QString("product="+Product+"&application=adcvalidator")+
                 QString("&begin_date=")+StartString+QString("&end_date=")+EndString+
                 QString("&station=")+MarkerID.toString()+
                 QString("&time_zone=GMT&units=")+Units+
                 QString("&interval=&format=csv");
    if(Datum != "Stnd")RequestURL = RequestURL+QString("&datum=")+Datum;
    manager->get(QNetworkRequest(QUrl(RequestURL)));
}

