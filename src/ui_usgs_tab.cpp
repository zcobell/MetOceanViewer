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
    int ierr;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    //...Create a new USGS object
    if(!thisUSGS.isNull())
        delete thisUSGS;
    thisUSGS = new usgs(ui->usgs_map,ui->usgs_graphics,
                        ui->radio_usgsDaily,ui->radio_usgshistoric,
                        ui->radio_usgs_instant,ui->combo_USGSProduct,
                        ui->Date_usgsStart,ui->Date_usgsEnd,ui->statusBar,this);

    ierr = thisUSGS->plotNewUSGSStation();
    QApplication::restoreOverrideCursor();

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
    if(!thisUSGS.isNull())
        thisUSGS->setUSGSBeenPlotted(false);
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
    if(!thisUSGS.isNull())
        thisUSGS->setUSGSBeenPlotted(false);
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
    if(!thisUSGS.isNull())
        thisUSGS->setUSGSBeenPlotted(false);
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
    int ierr;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ierr = thisUSGS->replotCurrentUSGSStation(index);
    QApplication::restoreOverrideCursor();
    if(ierr!=0)
        QMessageBox::critical(this,"ERROR",thisUSGS->getUSGSErrorString());
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Function to save the map and chart as a jpg
//-------------------------------------------//
void MainWindow::on_button_usgssavemap_clicked()
{
    QString filename;

    QString MarkerID = thisUSGS->getLoadedUSGSStation();
    QString MarkerID2 = thisUSGS->getClickedUSGSStation();

    if(MarkerID=="none")
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    if(MarkerID != MarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    if(!thisUSGS->getUSGSBeenPlotted())
    {
        QMessageBox::critical(this,"ERROR","Plot the data before attempting to save.");
        return;
    }

    QString filter = "JPG (*.jpg *.jpeg)";
    QString DefaultFile = "/USGS_"+MarkerID+".jpg";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"JPG (*.jpg *.jpeg) ;; PDF (*.pdf)",&filter);

    if(TempString==NULL)
        return;

    splitPath(TempString,filename,PreviousDirectory);

    thisUSGS->saveUSGSImage(TempString,filter);

    return;

}
//-------------------------------------------//


//-------------------------------------------//
//Saves the USGS data as an IMEDS formatted file
//or a CSV
//-------------------------------------------//
void MainWindow::on_button_usgssavedata_clicked()
{
    QString filename;

    QString MarkerID = thisUSGS->getLoadedUSGSStation();
    QString MarkerID2 = thisUSGS->getClickedUSGSStation();

    if(MarkerID=="none")
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    if(MarkerID != MarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    if(!thisUSGS->getUSGSBeenPlotted())
    {
        QMessageBox::critical(this,"ERROR","Plot the data before attempting to save.");
        return;
    }

    QString filter;
    QString DefaultFile = "/USGS_"+MarkerID+".imeds";

    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                                    PreviousDirectory+DefaultFile,
                                    "IMEDS (*.imeds);;CSV (*.csv)",&filter);

    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(TempString == NULL)
        return;

    splitPath(TempString,filename,PreviousDirectory);

    thisUSGS->saveUSGSData(TempString,format);

    return;
}
//-------------------------------------------//

void MainWindow::on_button_usgsresetzoom_clicked()
{
    if(!this->thisUSGS.isNull())
        ui->usgs_graphics->resetZoom();
    return;
}
