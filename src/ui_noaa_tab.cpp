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
    int ierr;

    int MarkerID = thisNOAA->getLoadedNOAAStation();
    int MarkerID2 = thisNOAA->getClickedNOAAStation();

    if(MarkerID==-1)
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    if(MarkerID != MarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter = "PDF (*.pdf)";
    QString DefaultFile = "/NOAA_"+QString::number(MarkerID)+".pdf";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory+DefaultFile,"PDF(*.pdf)",&filter);

    if(TempString==NULL)
        return;

    splitPath(TempString,filename,this->PreviousDirectory);

    ierr = thisNOAA->saveNOAAImage(TempString);

    return;

}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user tries to save the NOAA data
//-------------------------------------------//
void MainWindow::on_button_noaasavedata_clicked()
{

    QString filename;

    int MarkerID = thisNOAA->getLoadedNOAAStation();
    int MarkerID2 = thisNOAA->getClickedNOAAStation();

    if(MarkerID==-1)
    {
        QMessageBox::critical(this,"ERROR","No Station has been selected.");
        return;
    }

    if(MarkerID != MarkerID2)
    {
        QMessageBox::critical(this,"ERROR","The currently selected station is not the data loaded.");
        return;
    }

    QString filter;
    QString DefaultFile = "/NOAA_"+QString::number(MarkerID)+".imeds";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",PreviousDirectory+DefaultFile,
                                                    "IMEDS (*.imeds);;CSV (*.csv)",&filter);
    QStringList filter2 = filter.split(" ");
    QString format = filter2.value(0);

    if(TempString == NULL)
        return;

    splitPath(TempString,filename,this->PreviousDirectory);

    thisNOAA->saveNOAAData(filename,this->PreviousDirectory,format);

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
    QString error,TempString;

    //Display the wait cursor
    QApplication::setOverrideCursor(Qt::WaitCursor);

    //...Create a new NOAA object
    if(!thisNOAA.isNull())
        delete thisNOAA;
    thisNOAA = new noaa(ui->noaa_map,ui->noaa_graphics,ui->Date_StartTime,
                        ui->Date_EndTime,ui->combo_NOAAProduct,ui->combo_noaaunits,
                        ui->combo_noaadatum,ui->statusBar,this);

    ierr = thisNOAA->plotNOAAStation();
    QApplication::restoreOverrideCursor();
    if(ierr!=0)
    {
        TempString = thisNOAA->getNOAAErrorString().split("Error: ").value(1);
        if(TempString.length()>0)
            error = TempString;
        else
            error = thisNOAA->getNOAAErrorString();
        QMessageBox::critical(this,"ERROR",error);
    }

    return;
}
//-------------------------------------------//

