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

    if(thisUSGS->USGSMarkerID=="none")
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    QVariant USGSMarkerID2 = QVariant();
    ui->usgs_map->page()->runJavaScript("returnStationID()",[&USGSMarkerID2](const QVariant &v){USGSMarkerID2 = v;});
    while(USGSMarkerID2.isNull())
        delayM(5);
    if(thisUSGS->USGSMarkerID != USGSMarkerID2.toString().split(";").value(0).mid(4))
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }
    if(!thisUSGS->USGSBeenPlotted)
    {
        QMessageBox::critical(this,"ERROR","Plot the data before attempting to save.");
        return;
    }

    QString filter = "PDF (*.PDF)";
    QString DefaultFile = "/USGS_"+thisUSGS->USGSMarkerID+".pdf";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"PDF (*.pdf)",&filter);

    if(TempString==NULL)
        return;

    splitPath(TempString,filename,PreviousDirectory);

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPrinter::Letter);
    printer.setResolution(400);
    printer.setOrientation(QPrinter::Landscape);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(TempString);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.begin(&printer);

    //...Page 1 - Chart
    ui->usgs_graphics->render(&painter);

    //...Page 2 - Map
    printer.newPage();
    QPixmap map = ui->usgs_map->grab();
    QPixmap mapScaled = map.scaledToWidth(printer.width());
    if(mapScaled.height()>printer.height())
        mapScaled = map.scaledToHeight(printer.height());
    int cw = (printer.width()-mapScaled.width())/2;
    int ch = (printer.height()-mapScaled.height())/2;
    painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

    painter.end();
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

    if(thisUSGS->USGSMarkerID=="none")
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    QVariant USGSMarkerID2 = QVariant();
    ui->usgs_map->page()->runJavaScript("returnStationID()",[&USGSMarkerID2](const QVariant &v){USGSMarkerID2 = v;});
    while(USGSMarkerID2.isNull())
        delayM(5);
    if(thisUSGS->USGSMarkerID != USGSMarkerID2.toString().split(";").value(0).mid(4))
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }
    if(!thisUSGS->USGSBeenPlotted)
    {
        QMessageBox::critical(this,"ERROR","Plot the data before attempting to save.");
        return;
    }

    QString filter;
    QString DefaultFile = "/USGS_"+thisUSGS->USGSMarkerID+".imeds";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory+DefaultFile,"IMEDS (*.imeds);;CSV (*.csv)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(TempString == NULL)
        return;

    splitPath(TempString,filename,PreviousDirectory);

    QFile USGSOutput(TempString);
    QTextStream Output(&USGSOutput);
    USGSOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+thisUSGS->USGSMarkerID+"\n";
        Output << "Datum: N/A\n";
        Output << "Units: N/A\n";
        Output << "\n";
        for(int i=0;i<thisUSGS->USGSPlot.length();i++)
        {
            Output << thisUSGS->USGSPlot[i].Date.toString("MM/dd/yyyy")+","+
                      thisUSGS->USGSPlot[i].Time.toString("hh:mm")+","+
                      QString::number(thisUSGS->USGSPlot[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec value\n";
        Output << "USGS   UTC    N/A\n";
        Output << "USGS_"+thisUSGS->USGSMarkerID+"   "+QString::number(thisUSGS->CurrentUSGSLat)+
                  "   "+QString::number(thisUSGS->CurrentUSGSLon)+"\n";
        for(int i=0;i<thisUSGS->USGSPlot.length();i++)
        {
            Output << thisUSGS->USGSPlot[i].Date.toString("yyyy")+"    "+
                        thisUSGS->USGSPlot[i].Date.toString("MM")+"    "+
                        thisUSGS->USGSPlot[i].Date.toString("dd")+"    "+
                        thisUSGS->USGSPlot[i].Time.toString("hh")+"    "+
                        thisUSGS->USGSPlot[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(thisUSGS->USGSPlot[i].value)+"\n";
        }

    }
    USGSOutput.close();
    return;
}
//-------------------------------------------//



