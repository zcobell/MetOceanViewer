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

    QString filter = "PDF (*.PDF)";
    QString DefaultFile = "/USGS_"+MarkerID+".pdf";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"PDF (*.pdf)",&filter);

    if(TempString==NULL)
        return;

    splitPath(TempString,filename,PreviousDirectory);

    thisUSGS->saveUSGSImage(TempString);

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

