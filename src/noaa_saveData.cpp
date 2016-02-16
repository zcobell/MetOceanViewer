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
#include <noaa.h>

int noaa::saveNOAAImage(QString filename, QString filter)
{

    if(filter == "PDF (*.pdf)")
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
        this->chart->render(&painter);

        //...Page 2 - Map
        printer.newPage();
        QPixmap renderedMap = this->map->grab();
        QPixmap mapScaled = renderedMap.scaledToWidth(printer.width());
        if(mapScaled.height()>printer.height())
            mapScaled = renderedMap.scaledToHeight(printer.height());
        int cw = (printer.width()-mapScaled.width())/2;
        int ch = (printer.height()-mapScaled.height())/2;
        painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

        painter.end();
    }
    else if(filter == "JPG (*.jpg *.jpeg)")
    {
        QFile outputFile(filename);
        QSize imageSize(this->map->size().width()+this->chart->size().width(),this->map->size().height());
        QRect chartRect(this->map->size().width(),0,this->chart->size().width(),this->chart->size().height());

        QImage pixmap(imageSize, QImage::Format_ARGB32);
        pixmap.fill(Qt::white);
        QPainter imagePainter(&pixmap);
        imagePainter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        this->map->render(&imagePainter,QPoint(0,0));
        this->chart->render(&imagePainter,chartRect);

        outputFile.open(QIODevice::WriteOnly);
        pixmap.save(&outputFile,"JPG",100);
    }

    return 0;
}

int noaa::saveNOAAData(QString filename, QString PreviousDirectory, QString format)
{
    int index;
    QString value,filename2;

    for(index=0;index<this->CurrentNOAAStation.length();index++)
    {
        if(this->CurrentNOAAStation.length()==2)
        {
            if(index==0)
                filename2 = PreviousDirectory+"/Observation_"+filename;
            else
                filename2 = PreviousDirectory+"/Predictions_"+filename;
        }
        else
            filename2 = filename;

        QFile NOAAOutput(filename2);

        QTextStream Output(&NOAAOutput);
        NOAAOutput.open(QIODevice::WriteOnly);

        if(format.compare("CSV")==0)
        {
            Output << "Station: "+QString::number(this->NOAAMarkerID)+"\n";
            Output << "Datum: "+this->noaaDatum->currentText()+"\n";
            Output << "Units: "+this->noaaUnits->currentText()+"\n";
            Output << "\n";
            for(int i=0;i<this->CurrentNOAAStation.length();i++)
            {
                value.sprintf("%10.4e",this->CurrentNOAAStation[index][i].value);
                Output << this->CurrentNOAAStation[index][i].Date.toString("MM/dd/yyyy")+","+
                          this->CurrentNOAAStation[index][i].Time.toString("hh:mm")+","+
                          value+"\n";
            }
        }
        else if(format.compare("IMEDS")==0)
        {
            QString datum = this->noaaDatum->currentText();
            QString units = this->noaaUnits->currentText();
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
            Output << "NOAA_"+QString::number(this->NOAAMarkerID)+
                      "   "+QString::number(this->CurrentNOAALat)+
                      "   "+QString::number(this->CurrentNOAALon)+"\n";
            for(int i=0;i<this->CurrentNOAAStation[index].length();i++)
            {
                value.sprintf("%10.4e",this->CurrentNOAAStation[index][i].value);
                Output << this->CurrentNOAAStation[index][i].Date.toString("yyyy")+"    "+
                            this->CurrentNOAAStation[index][i].Date.toString("MM")+"    "+
                            this->CurrentNOAAStation[index][i].Date.toString("dd")+"    "+
                            this->CurrentNOAAStation[index][i].Time.toString("hh")+"    "+
                            this->CurrentNOAAStation[index][i].Time.toString("mm")+"    "+
                                                            "00" +"    "+value+"\n";
            }

        }
        NOAAOutput.close();
    }

    return 0;
}
