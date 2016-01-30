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
//Send the data to the panTo function
//-------------------------------------------//
void MainWindow::on_combo_usgs_panto_currentIndexChanged(int index)
{
    ui->usgs_map->page()->runJavaScript("panTo('"+ui->combo_usgs_panto->currentText()+"')");
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Function to fetch the data from the USGS
//server
//-------------------------------------------//
void MainWindow::on_button_usgs_fetch_clicked()
{
    int i,ierr;

    //...Create a new USGS object
    if(!thisUSGS.isNull())
        delete thisUSGS;
    thisUSGS = new usgs(this);

    //...Check the data type
    if(ui->radio_usgs_instant->isChecked())
        thisUSGS->USGSdataMethod = 1;
    else if(ui->radio_usgsDaily->isChecked())
        thisUSGS->USGSdataMethod = 2;
    else
        thisUSGS->USGSdataMethod = 0;

    //...Display the wait cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ui->statusBar->showMessage("Downloading data from USGS...");

    //...Wipe out the combo box
    ui->combo_USGSProduct->clear();

    //...Retrieve info from google maps
    QVariant eval = QVariant();
    ui->usgs_map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    QStringList evalList = eval.toString().split(";");
    thisUSGS->USGSMarkerString = evalList.value(0).mid(4);

    //...Sanity Check
    if(evalList.value(0)=="none")
    {
        QMessageBox::warning(this,"Warning","No station has been selected.");
        ui->statusBar->clearMessage();
        QApplication::restoreOverrideCursor();
        return;
    }

    //...Store the station information
    thisUSGS->USGSMarkerID = thisUSGS->USGSMarkerString;
    thisUSGS->CurrentUSGSStationName = evalList.value(1).simplified();
    thisUSGS->CurrentUSGSLon = evalList.value(2).toDouble();
    thisUSGS->CurrentUSGSLat = evalList.value(3).toDouble();

    //...Get the time period for the data
    thisUSGS->requestEndDate = ui->Date_usgsEnd->dateTime();
    thisUSGS->requestStartDate = ui->Date_usgsStart->dateTime();

    //...Grab the data from the server
    ierr = thisUSGS->fetchUSGSData();
    if(ierr!=0)
    {
        QApplication::restoreOverrideCursor();
        ui->statusBar->clearMessage();
        QMessageBox::critical(this,"ERROR",thisUSGS->USGSErrorString);
        return;
    }

    //...Update the combo box
    for(i=0;i<thisUSGS->Parameters.length();i++)
        ui->combo_USGSProduct->addItem(thisUSGS->Parameters[i]);
    ui->combo_USGSProduct->setCurrentIndex(0);
    thisUSGS->ProductName = ui->combo_USGSProduct->currentText();

    //...Plot the first series
    ierr = thisUSGS->plotUSGS(ui->usgs_graphics);
    if(ierr!=0)
    {
        QApplication::restoreOverrideCursor();
        ui->statusBar->clearMessage();
        QMessageBox::critical(this,"ERROR","No data available for this selection.");
        return;
    }

    //...Restore the mouse pointer
    QApplication::restoreOverrideCursor();
    ui->statusBar->clearMessage();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Sets the data range when the usgs instant
//radio button is clicked since the "instant"
//data is only available 120 days into the past
//-------------------------------------------//
void MainWindow::on_radio_usgs_instant_clicked()
{
    thisUSGS->USGSBeenPlotted = false;
    ui->Date_usgsStart->setMinimumDateTime(QDateTime::currentDateTime().addDays(-120));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime::currentDateTime().addDays(-120));

    if(ui->Date_usgsStart->dateTime()<QDateTime::currentDateTime().addDays(-120))
        ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-120));

    if(ui->Date_usgsEnd->dateTime()<QDateTime::currentDateTime().addDays(-120))
        ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime().addDays(-120));

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Resets the minimum datetime to a long time
//ago when using daily data from USGS
//-------------------------------------------//
void MainWindow::on_radio_usgsDaily_clicked()
{
    thisUSGS->USGSBeenPlotted = false;
    ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Resets the minimum datetime to a long time
//ago when using daily data from USGS
//-------------------------------------------//
void MainWindow::on_radio_usgshistoric_clicked()
{
    thisUSGS->USGSBeenPlotted = false;
    ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900,1,1)));
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Fires when the combo box is changed and
//plots the data immediately
//-------------------------------------------//
void MainWindow::on_combo_USGSProduct_currentIndexChanged(int index)
{
    QString javascript;
    thisUSGS->USGSBeenPlotted = false;
    if(thisUSGS->USGSDataReady)
    {
        thisUSGS->ProductIndex = index;
        thisUSGS->ProductName = ui->combo_USGSProduct->currentText();
        thisUSGS->plotUSGS(ui->usgs_graphics);
        ui->usgs_map->page()->runJavaScript(javascript);
    }
    return;
}
//-------------------------------------------//
