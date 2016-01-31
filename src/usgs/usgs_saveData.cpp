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
#include <usgs.h>

int usgs::saveUSGSImage(QString filename)
{

    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPrinter::Letter);
    printer.setResolution(400);
    printer.setOrientation(QPrinter::Landscape);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filename);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.begin(&printer);

    //...Page 1 - Chart
    chart->render(&painter);

    //...Page 2 - Map
    printer.newPage();
    QPixmap renderedMap = map->grab();
    QPixmap mapScaled = renderedMap.scaledToWidth(printer.width());
    if(mapScaled.height()>printer.height())
        mapScaled = renderedMap.scaledToHeight(printer.height());
    int cw = (printer.width()-mapScaled.width())/2;
    int ch = (printer.height()-mapScaled.height())/2;
    painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

    painter.end();
}


int usgs::saveUSGSData(QString filename, QString format)
{
    QFile USGSOutput(filename);
    QTextStream Output(&USGSOutput);
    USGSOutput.open(QIODevice::WriteOnly);

    if(format.compare("CSV")==0)
    {
        Output << "Station: "+this->USGSMarkerID+"\n";
        Output << "Datum: N/A\n";
        Output << "Units: N/A\n";
        Output << "\n";
        for(int i=0;i<this->USGSPlot.length();i++)
        {
            Output << this->USGSPlot[i].Date.toString("MM/dd/yyyy")+","+
                      this->USGSPlot[i].Time.toString("hh:mm")+","+
                      QString::number(this->USGSPlot[i].value)+"\n";
        }
    }
    else if(format.compare("IMEDS")==0)
    {
        Output << "% IMEDS generic format - Water Level\n";
        Output << "% year month day hour min sec value\n";
        Output << "USGS   UTC    N/A\n";
        Output << "USGS_"+this->USGSMarkerID+"   "+QString::number(this->CurrentUSGSLat)+
                  "   "+QString::number(this->CurrentUSGSLon)+"\n";
        for(int i=0;i<this->USGSPlot.length();i++)
        {
            Output << this->USGSPlot[i].Date.toString("yyyy")+"    "+
                        this->USGSPlot[i].Date.toString("MM")+"    "+
                        this->USGSPlot[i].Date.toString("dd")+"    "+
                        this->USGSPlot[i].Time.toString("hh")+"    "+
                        this->USGSPlot[i].Time.toString("mm")+"    "+
                                                        "00" +"    "+
                        QString::number(this->USGSPlot[i].value)+"\n";
        }

    }
    USGSOutput.close();
}
