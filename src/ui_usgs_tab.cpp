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
    QString RequestURL;
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);

    //Retrieve info from google maps
    QVariant eval = ui->usgs_map->page()->mainFrame()->evaluateJavaScript("returnStationID()");
    QStringList evalList = eval.toString().split(";");
    USGSMarkerID = evalList.value(0).toInt();
    CurrentUSGSStationName = evalList.value(1).simplified();
    CurrentUSGSLat = evalList.value(2).toDouble();
    CurrentUSGSLon = evalList.value(3).toDouble();

    if(USGSMarkerID==-1)
    {
        QMessageBox::warning(this,"Warning","No station has been selected.");
        return;
    }

    //Connect the finished downloading signal to the routine that plots the markers
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(ReadUSGSDataFinished(QNetworkReply*)));
    QUrl url = QString::fromLocal8Bit("http://waterservices.usgs.gov/nwis/iv/?sites=08313000&period=P7D&format=rdb");
    manager->get(QNetworkRequest(url));

    return;
}
