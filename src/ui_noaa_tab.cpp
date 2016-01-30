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
#include "noaa.h"

//-------------------------------------------//
//Called when the pan to combo box is updated
//-------------------------------------------//
void MainWindow::on_Combo_NOAAPanTo_currentIndexChanged(int index)
{
    ui->noaa_map->page()->runJavaScript("panTo('"+ui->Combo_NOAAPanTo->currentText()+"')");
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user attempts to save the NOAA chart
//-------------------------------------------//
void MainWindow::on_button_noaasavechart_clicked()
{
    QString filename;

    if(this->thisNOAA->NOAAMarkerID==-1)
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    QVariant eval = QVariant();
    ui->noaa_map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    QStringList evalList = eval.toString().split(";");
    int NOAAMarkerID2 = evalList.value(0).toInt();
    if(this->thisNOAA->NOAAMarkerID != NOAAMarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter = "PDF (*.pdf)";
    QString DefaultFile = "/NOAA_"+QString::number(this->thisNOAA->NOAAMarkerID)+".pdf";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"PDF(*.pdf)",&filter);

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
    ui->noaa_graphics->render(&painter);

    //...Page 2 - Map
    printer.newPage();
    QPixmap map = ui->noaa_map->grab();
    QPixmap mapScaled = map.scaledToWidth(printer.width());
    if(mapScaled.height()>printer.height())
        mapScaled = map.scaledToHeight(printer.height());
    int cw = (printer.width()-mapScaled.width())/2;
    int ch = (printer.height()-mapScaled.height())/2;
    painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

    painter.end();

}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user tries to save the NOAA data
//-------------------------------------------//
void MainWindow::on_button_noaasavedata_clicked()
{

    int index;
    QString Filename2,Filename3;
    QString value;

    if(this->thisNOAA->NOAAMarkerID==-1)
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }
    QVariant eval = QVariant();
    ui->noaa_map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    QStringList evalList = eval.toString().split(";");
    int NOAAMarkerID2 = evalList.value(0).toInt();
    if(this->thisNOAA->NOAAMarkerID != NOAAMarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter;
    QString DefaultFile = "/NOAA_"+QString::number(this->thisNOAA->NOAAMarkerID)+".imeds";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory+DefaultFile,
                                                    "IMEDS (*.imeds);;CSV (*.csv)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(TempString == NULL)
        return;

    splitPath(TempString,Filename2,PreviousDirectory);

    for(index=0;index<thisNOAA->CurrentNOAAStation.length();index++)
    {
        if(thisNOAA->CurrentNOAAStation.length()==2)
        {
            if(index==0)
                Filename3 = PreviousDirectory+"/Observation_"+Filename2;
            else
                Filename3 = PreviousDirectory+"/Predictions_"+Filename2;
        }
        else
            Filename3 = TempString;

        QFile NOAAOutput(Filename3);

        QTextStream Output(&NOAAOutput);
        NOAAOutput.open(QIODevice::WriteOnly);

        if(format.compare("CSV")==0)
        {
            Output << "Station: "+QString::number(this->thisNOAA->NOAAMarkerID)+"\n";
            Output << "Datum: "+ui->combo_noaadatum->currentText()+"\n";
            Output << "Units: "+ui->combo_noaaunits->currentText()+"\n";
            Output << "\n";
            for(int i=0;i<thisNOAA->CurrentNOAAStation.length();i++)
            {
                value.sprintf("%10.4e",thisNOAA->CurrentNOAAStation[index][i].value);
                Output << thisNOAA->CurrentNOAAStation[index][i].Date.toString("MM/dd/yyyy")+","+
                          thisNOAA->CurrentNOAAStation[index][i].Time.toString("hh:mm")+","+
                          value+"\n";
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
            Output << "NOAA_"+QString::number(this->thisNOAA->NOAAMarkerID)+
                      "   "+QString::number(this->thisNOAA->CurrentNOAALat)+
                      "   "+QString::number(this->thisNOAA->CurrentNOAALon)+"\n";
            for(int i=0;i<thisNOAA->CurrentNOAAStation[index].length();i++)
            {
                value.sprintf("%10.4e",thisNOAA->CurrentNOAAStation[index][i].value);
                Output << thisNOAA->CurrentNOAAStation[index][i].Date.toString("yyyy")+"    "+
                            thisNOAA->CurrentNOAAStation[index][i].Date.toString("MM")+"    "+
                            thisNOAA->CurrentNOAAStation[index][i].Date.toString("dd")+"    "+
                            thisNOAA->CurrentNOAAStation[index][i].Time.toString("hh")+"    "+
                            thisNOAA->CurrentNOAAStation[index][i].Time.toString("mm")+"    "+
                                                            "00" +"    "+value+"\n";
            }

        }
        NOAAOutput.close();
    }
    return;
}
//-------------------------------------------//

void MainWindow::on_combo_NOAAProduct_currentIndexChanged(int index)
{
    if(index==3)
    {
        ui->Date_StartTime->setMaximumDateTime(QDateTime::fromString("2100/01/01 00:00","yyyy/MM/dd hh:mm"));
        ui->Date_EndTime->setMaximumDateTime(QDateTime::fromString("2100/01/01 00:00","yyyy/MM/dd hh:mm"));
    }
    else
    {
        if(ui->Date_EndTime->dateTime()>QDateTime::currentDateTime())
            ui->Date_EndTime->setDateTime(QDateTime::currentDateTime());
        if(ui->Date_StartTime->dateTime()>QDateTime::currentDateTime())
            ui->Date_StartTime->setDateTime(QDateTime::currentDateTime().addDays(-1));
        ui->Date_StartTime->setMaximumDateTime(QDateTime::currentDateTime());
        ui->Date_EndTime->setMaximumDateTime(QDateTime::currentDateTime());
    }
    return;
}

//-------------------------------------------//
//Called when the NOAA fetch data button is clicked
//-------------------------------------------//
void MainWindow::on_Button_FetchData_clicked()
{
    int ierr;

    //...Create a new NOAA object
    if(!thisNOAA.isNull())
        delete thisNOAA;
    thisNOAA = new noaa(this);

    //...Grab the station from the javascript
    QVariant eval = QVariant();
    ui->noaa_map->page()->runJavaScript("returnStationID()",[&eval](const QVariant &v){eval = v;});
    while(eval.isNull())
        delayM(5);
    QStringList evalList = eval.toString().split(";");

    //...Grab the options from the UI
    thisNOAA->NOAAMarkerID = evalList.value(0).toInt();
    thisNOAA->CurrentNOAAStationName = evalList.value(1).simplified();
    thisNOAA->CurrentNOAALat = evalList.value(3).toDouble();
    thisNOAA->CurrentNOAALon = evalList.value(2).toDouble();
    thisNOAA->StartDate = ui->Date_StartTime->dateTime();
    thisNOAA->EndDate = ui->Date_EndTime->dateTime();
    thisNOAA->Units = ui->combo_noaaunits->currentText();
    thisNOAA->Datum = ui->combo_noaadatum->currentText();
    thisNOAA->ProductIndex = ui->combo_NOAAProduct->currentIndex();

    //Display the wait cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);

    //Update status
    ui->statusBar->showMessage("Downloading data from NOAA...",0);

    //...Generate the javascript calls in this array
    ierr = thisNOAA->fetchNOAAData();

    //...Update the status bar
    ui->statusBar->showMessage("Plotting the data from NOAA...");

    //...Generate prep the data for plotting
    ierr = thisNOAA->prepNOAAResponse();

    //...Check for valid data
    if(thisNOAA->CurrentNOAAStation[0].length()<5)
    {
        ui->statusBar->clearMessage();
        QApplication::restoreOverrideCursor();
        QMessageBox::information(this,"ERROR",thisNOAA->ErrorString[0]);
        return;
    }

    //...Plot the chart
    ierr = thisNOAA->plotChart(ui->noaa_graphics);

    //...Clear messages and cursors
    ui->statusBar->clearMessage();
    QApplication::restoreOverrideCursor();

    return;
}
//-------------------------------------------//

