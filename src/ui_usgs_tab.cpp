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
// $Author
// $Date$
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: ui_usgs_tab.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"

//Send the data to the panTo function
void MainWindow::on_combo_usgs_panto_currentIndexChanged(int index)
{
    ui->usgs_map->page()->mainFrame()->evaluateJavaScript("panTo('"+ui->combo_usgs_panto->currentText()+"')");
    return;
}

void MainWindow::on_button_usgs_fetch_clicked()
{
    QString RequestURL,USGSMarkerString;
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QDateTime startDate,endDate;
    QString startDateString,endDateString;

    USGSbeenPlotted = false;
    USGSdataReady = false;

    //Wipe out the combo box
    ui->combo_USGSProduct->clear();

    //Retrieve info from google maps
    QVariant eval = ui->usgs_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");

    //Sanity Check
    if(evalList.value(0)=="none")
    {
        QMessageBox::warning(this,"Warning","No station has been selected.");
        return;
    }

    //Get the station information
    USGSMarkerString = evalList.value(0).mid(4);
    USGSMarkerID = USGSMarkerString;
    CurrentUSGSStationName = evalList.value(1).simplified();
    CurrentUSGSLat = evalList.value(2).toDouble();
    CurrentUSGSLon = evalList.value(3).toDouble();

    //Get the time period for the data
    endDate = ui->Date_usgsEnd->dateTime();
    startDate = ui->Date_usgsStart->dateTime();
    endDateString = "&endDT="+endDate.toString("yyyy-MM-dd");
    startDateString = "&startDT="+startDate.toString("yyyy-MM-dd");

    //Connect the finished downloading signal to the routine that plots the markers
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(ReadUSGSDataFinished(QNetworkReply*)));

    //Construct the correct request URL
    if(USGSdataMethod==0)
        RequestURL = "http://nwis.waterdata.usgs.gov/nwis/uv?format=rdb&site_no="+USGSMarkerString+startDateString+endDateString;
    else if(USGSdataMethod==1)
        RequestURL = "http://waterservices.usgs.gov/nwis/iv/?sites="+USGSMarkerString+startDateString+endDateString+"&format=rdb";
    else
        RequestURL = "http://waterservices.usgs.gov/nwis/dv/?sites="+USGSMarkerString+startDateString+endDateString+"&format=rdb";

    //Make the request to the server
    manager->get(QNetworkRequest(QUrl(RequestURL)));

    return;
}

void MainWindow::on_radio_usgsInstant_clicked()
{
    USGSdataMethod = 1;
    ui->Date_usgsStart->setMinimumDateTime(QDateTime::currentDateTime().addDays(-120));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime::currentDateTime().addDays(-120));

    if(ui->Date_usgsStart->dateTime()<QDateTime::currentDateTime().addDays(-120))
        ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-120));

    if(ui->Date_usgsEnd->dateTime()<QDateTime::currentDateTime().addDays(-120))
        ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime().addDays(-120));

    return;
}

void MainWindow::on_radio_usgsDaily_clicked()
{
    USGSdataMethod = 2;
    ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    return;
}

void MainWindow::on_radio_usgshistoric_clicked()
{
    USGSdataMethod = 0;
    ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    return;
}
