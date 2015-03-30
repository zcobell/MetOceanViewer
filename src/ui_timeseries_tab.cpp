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
// $Author$
// $Date$
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: ui_timeseries_tab.cpp
//
//------------------------------------------------------------------------------

#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries.h>
#include <timeseries_add_data.h>

QVector<IMEDS> TimeseriesData;

//Called when the user tries to save the Timeseries image
void MainWindow::on_button_saveTimeseriesImage_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile TimeseriesOutput(Filename);
    QPixmap TimeseriesImage(ui->timeseries_map->size());
    ui->timeseries_map->render(&TimeseriesImage);
    TimeseriesOutput.open(QIODevice::WriteOnly);
    TimeseriesImage.save(&TimeseriesOutput,"JPG",100);

}

//Called when the user checks the timeseries auto y axis box
void MainWindow::on_check_TimeseriesYauto_toggled(bool checked)
{
    ui->spin_TimeseriesYmin->setEnabled(!checked);
    ui->spin_TimeseriesYmax->setEnabled(!checked);
}


//Adds a row to the table and reads the new data into the timeseries variable
void MainWindow::on_button_TimeseriesAddRow_clicked()
{
    add_imeds_data AddWindow;
    QColor CellColor;
    ADCNC NetCDFData;
    ADCASCII ADCData;

    EditBox = false;

    int NumberOfRows = ui->table_TimeseriesData->rowCount();
    AddWindow.setModal(false);
    AddWindow.set_default_dialog_box_elements(NumberOfRows);

    int WindowStatus = AddWindow.exec();

    QApplication::setOverrideCursor(Qt::WaitCursor);

    if(WindowStatus == 1)
    {

        NumberOfRows = NumberOfRows+1;
        TimeseriesData.resize(NumberOfRows);

        TimeseriesData[NumberOfRows-1].success = false;

        if(InputFileType=="IMEDS")
        {
            TimeseriesData[NumberOfRows-1] = readIMEDS(InputFilePath);
            UpdateTimeseriesDateRange(TimeseriesData[NumberOfRows-1]);
        }
        else if(InputFileType=="NETCDF")
        {
            NetCDFData = readADCIRCnetCDF(InputFilePath);
            if(!NetCDFData.success)
                TimeseriesData[NumberOfRows-1].success = false;
            else
            {
                TimeseriesData[NumberOfRows-1] = NetCDF_to_IMEDS(NetCDFData,InputFileColdStart);
                UpdateTimeseriesDateRange(TimeseriesData[NumberOfRows-1]);
            }
        }
        else if(InputFileType=="ADCIRC")
        {
            ADCData = readADCIRCascii(InputFilePath,StationFilePath);
            if(!ADCData.success)
                TimeseriesData[NumberOfRows-1].success = false;
            else
            {
                TimeseriesData[NumberOfRows-1] = ADCIRC_to_IMEDS(ADCData,InputFileColdStart);
                UpdateTimeseriesDateRange(TimeseriesData[NumberOfRows-1]);
            }
        }

        ui->table_TimeseriesData->setRowCount(NumberOfRows);
        ui->table_TimeseriesData->setItem(NumberOfRows-1,0,new QTableWidgetItem(InputFileName));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,1,new QTableWidgetItem(InputSeriesName));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,2,new QTableWidgetItem(InputColorString));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,3,new QTableWidgetItem(QString::number(UnitConversion)));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,4,new QTableWidgetItem(QString::number(xadjust)));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,5,new QTableWidgetItem(QString::number(yadjust)));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,6,new QTableWidgetItem(InputFilePath));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,7,
                                     new QTableWidgetItem(InputFileColdStart.toString("yyyy-MM-dd hh:mm:ss")));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,8,new QTableWidgetItem(InputFileType));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,9,new QTableWidgetItem(InputStationFile));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,10,new QTableWidgetItem(StationFilePath));
        CellColor.setNamedColor(InputColorString);
        ui->table_TimeseriesData->item(NumberOfRows-1,2)->setBackgroundColor(CellColor);

        //Tooltips in table cells
        ui->table_TimeseriesData->item(NumberOfRows-1,0)->setToolTip(InputFilePath);
        ui->table_TimeseriesData->item(NumberOfRows-1,1)->setToolTip(InputSeriesName);
        ui->table_TimeseriesData->item(NumberOfRows-1,3)->setToolTip(QString::number(UnitConversion));
        ui->table_TimeseriesData->item(NumberOfRows-1,4)->setToolTip(QString::number(xadjust));
        ui->table_TimeseriesData->item(NumberOfRows-1,5)->setToolTip(QString::number(yadjust));

        if(!TimeseriesData[NumberOfRows-1].success)
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            TimeseriesData.remove(NumberOfRows-1);
            ui->table_TimeseriesData->removeRow(NumberOfRows-1);
            QMessageBox::information(this,"ERROR","This file could not be read correctly.");
            QApplication::restoreOverrideCursor();
            return;
        }

    }

    QApplication::restoreOverrideCursor();
    AddWindow.close();
    return;
}

//Called when the delete row button is clicked. Removes from the
//table as well as the data vector
void MainWindow::on_button_TimeseriesDeleteRow_clicked()
{
    if(ui->table_TimeseriesData->rowCount()==0)
    {
        QMessageBox::information(this,"ERROR","There are no rows in the table.");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    //Reload the page so that arrays are refreshed.
    //This is sort of cheating, but it works
    ui->timeseries_map->reload();
    delay(2);

    TimeseriesData.remove(ui->table_TimeseriesData->currentRow());
    ui->table_TimeseriesData->removeRow(ui->table_TimeseriesData->currentRow());

    QApplication::restoreOverrideCursor();

    return;
}

void MainWindow::SetupTimeseriesTable()
{
    QString HeaderString = QString("Filename;Series Name;Color;Unit Conversion;")+
                           QString("x-shift;y-shift;FullPathToFile;Cold Start;")+
                           QString("FileType;StationFile;StationFilePath");
    QStringList Header = HeaderString.split(";");

    ui->table_TimeseriesData->setRowCount(0);
    ui->table_TimeseriesData->setColumnCount(11);
    ui->table_TimeseriesData->setColumnHidden(6,true);
    ui->table_TimeseriesData->setColumnHidden(7,true);
    ui->table_TimeseriesData->setColumnHidden(8,true);
    ui->table_TimeseriesData->setColumnHidden(9,true);
    ui->table_TimeseriesData->setColumnHidden(10,true);
    ui->table_TimeseriesData->setHorizontalHeaderLabels(Header);
    ui->table_TimeseriesData->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->table_TimeseriesData->setEditTriggers(QAbstractItemView::NoEditTriggers);

    return;
}

//Called when the edit row button is clicked. Updates the timeseries data vector
void MainWindow::on_button_TimeseriesEditRow_clicked()
{
    add_imeds_data AddWindow;
    QColor CellColor;
    QString Filename,Filepath,SeriesName,FileType;
    QDateTime ColdStart;

    int CurrentRow;

    EditBox = true;

    if(ui->table_TimeseriesData->rowCount() == 0)
    {
        QMessageBox::information(this,"ERROR","Insert a row first.");
        return;
    }

    if(ui->table_TimeseriesData->currentRow() == -1)
    {
        QMessageBox::information(this,"ERROR","No row selected.");
        return;
    }

    AddWindow.setModal(false);
    CurrentRow = ui->table_TimeseriesData->currentRow();
    Filename = ui->table_TimeseriesData->item(CurrentRow,0)->text();
    Filepath = ui->table_TimeseriesData->item(CurrentRow,6)->text();
    SeriesName = ui->table_TimeseriesData->item(CurrentRow,1)->text();
    UnitConversion = ui->table_TimeseriesData->item(CurrentRow,3)->text().toDouble();
    xadjust = ui->table_TimeseriesData->item(CurrentRow,4)->text().toDouble();
    yadjust = ui->table_TimeseriesData->item(CurrentRow,5)->text().toDouble();
    FileType = ui->table_TimeseriesData->item(CurrentRow,8)->text();
    ColdStart = QDateTime::fromString(ui->table_TimeseriesData->
                                      item(CurrentRow,7)->text().simplified(),"yyyy-MM-dd hh:mm:ss");
    CellColor.setNamedColor(ui->table_TimeseriesData->item(CurrentRow,2)->text());
    StationFilePath = ui->table_TimeseriesData->item(CurrentRow,10)->text();

    AddWindow.set_dialog_box_elements(Filename,Filepath,SeriesName,
                                      UnitConversion,xadjust,yadjust,
                                      CellColor,ColdStart,FileType,StationFilePath);

    int WindowStatus = AddWindow.exec();

    if(WindowStatus == 1)
    {
        ui->table_TimeseriesData->setItem(CurrentRow,0,new QTableWidgetItem(InputFileName));
        ui->table_TimeseriesData->setItem(CurrentRow,1,new QTableWidgetItem(InputSeriesName));
        ui->table_TimeseriesData->setItem(CurrentRow,3,new QTableWidgetItem(QString::number(UnitConversion)));
        ui->table_TimeseriesData->setItem(CurrentRow,4,new QTableWidgetItem(QString::number(xadjust)));
        ui->table_TimeseriesData->setItem(CurrentRow,5,new QTableWidgetItem(QString::number(yadjust)));
        ui->table_TimeseriesData->setItem(CurrentRow,6,new QTableWidgetItem(InputFilePath));
        ui->table_TimeseriesData->setItem(CurrentRow,7,new QTableWidgetItem(
                                         InputFileColdStart.toString("yyyy-MM-dd hh:mm:ss")));
        ui->table_TimeseriesData->setItem(CurrentRow,8,new QTableWidgetItem(InputFileType));
        ui->table_TimeseriesData->setItem(CurrentRow,9,new QTableWidgetItem(InputStationFile));
        ui->table_TimeseriesData->setItem(CurrentRow,10,new QTableWidgetItem(StationFilePath));

        //Tooltips in table cells
        ui->table_TimeseriesData->item(CurrentRow,0)->setToolTip(InputFilePath);
        ui->table_TimeseriesData->item(CurrentRow,1)->setToolTip(InputSeriesName);
        ui->table_TimeseriesData->item(CurrentRow,3)->setToolTip(QString::number(UnitConversion));
        ui->table_TimeseriesData->item(CurrentRow,4)->setToolTip(QString::number(xadjust));
        ui->table_TimeseriesData->item(CurrentRow,5)->setToolTip(QString::number(yadjust));

        if(ColorUpdated)
        {
            ui->table_TimeseriesData->setItem(CurrentRow,2,new QTableWidgetItem(InputColorString));
            CellColor.setNamedColor(InputColorString);
            ui->table_TimeseriesData->item(CurrentRow,2)->setBackgroundColor(CellColor);
        }

        //If we need to, read the new IMEDS file into the appropriate slot
        if(InputFilePath!=Filepath || ColdStart!=InputFileColdStart)
        {
            if(InputFileType=="IMEDS")
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                TimeseriesData.remove(CurrentRow);
                TimeseriesData.insert(CurrentRow,readIMEDS(InputFilePath));
                if(!TimeseriesData[CurrentRow].success)
                {
                    TimeseriesData.remove(CurrentRow);
                    ui->table_TimeseriesData->removeRow(CurrentRow);
                    QApplication::restoreOverrideCursor();
                    QMessageBox::information(this,"ERROR","This IMEDS file could not be read correctly.");
                    return;
                }
                QApplication::restoreOverrideCursor();
            }
            else if(InputFileType=="NETCDF")
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                TimeseriesData.remove(CurrentRow);
                TimeseriesData.insert(CurrentRow,NetCDF_to_IMEDS(readADCIRCnetCDF(InputFilePath),InputFileColdStart));
                if(!TimeseriesData[CurrentRow].success)
                {
                    TimeseriesData.remove(CurrentRow);
                    ui->table_TimeseriesData->removeRow(CurrentRow);
                    QApplication::restoreOverrideCursor();
                    QMessageBox::information(this,"ERROR","This NETCDF file could not be read correctly.");
                    return;
                }
                QApplication::restoreOverrideCursor();
            }
            else if(InputFileType=="ADCIRC")
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                TimeseriesData.remove(CurrentRow);
                TimeseriesData.insert(CurrentRow,
                    ADCIRC_to_IMEDS(readADCIRCascii(InputFilePath,StationFilePath),InputFileColdStart));
                if(!TimeseriesData[CurrentRow].success)
                {
                    TimeseriesData.remove(CurrentRow);
                    ui->table_TimeseriesData->removeRow(CurrentRow);
                    QApplication::restoreOverrideCursor();
                    QMessageBox::information(this,"ERROR","This ADCIRC file could not be read correctly.");
                    return;
                }
                QApplication::restoreOverrideCursor();
            }
            UpdateTimeseriesDateRange(TimeseriesData[CurrentRow]);
        }

    }
    AddWindow.close();
    return;
}


//Send the data to the HTML side of the code for plotting
void MainWindow::on_button_processTimeseriesData_clicked()
{
    int i,ierr;
    double x,y,YMin,YMax;
    bool Checked;
    QString javascript,name,color;
    QString PlotTitle,XLabel,YLabel,AutoY;
    QString AutoX,XMin,XMax;
    QString unit,plusX,plusY;
    QVariant jsResponse;

    //Change the mouse pointer
    QApplication::setOverrideCursor(Qt::WaitCursor);

    //Start by clearing the markers
    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    delay(1);

    if(ui->table_TimeseriesData->rowCount()==0)
    {
        QMessageBox::information(this,"ERROR","There are no rows in the table.");
        QApplication::restoreOverrideCursor();
        return;
    }

    //Set up our axis labels and plot titles
    PlotTitle = ui->text_TimeseriesPlotTitle->text();
    XLabel  = ui->text_TimeseriesXaxisLabel->text();
    YLabel  = ui->text_TimeseriesYaxisLabel->text();
    YMin    = ui->spin_TimeseriesYmin->value();
    YMax    = ui->spin_TimeseriesYmax->value();
    XMin    = ui->date_TimeseriesStartDate->dateTime().toString("yyyy-MM-dd");
    XMax    = ui->date_TimeseriesEndDate->dateTime().toString("yyyy-MM-dd");

    Checked = ui->check_TimeseriesYauto->isChecked();
    if(Checked)
        AutoY = "auto";
    else
        AutoY = "none";

    Checked = ui->check_TimeseriesAllData->isChecked();
    if(Checked)
        AutoX = "auto";
    else
        AutoX = "none";

    javascript = "setGlobal('"+PlotTitle+"','"+AutoY+"',"+QString::number(YMin)+
            ","+QString::number(YMax)+",'"+XLabel+"','"+YLabel+"','"+AutoX+"','"+
            XMin+"','"+XMax+"')";

    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript(javascript);

    //Verify that the stations are the same in all files
    if(TimeseriesData.length()>1)
    {        
        for(i=1;i<TimeseriesData.length();i++)
        {
            ierr = CheckStationLocationsTimeseries(TimeseriesData[0],TimeseriesData[i]);
            if(ierr==1)
            {
                QMessageBox::information(this,"ERROR","The station locations in the timeseries files do not match.");
                QApplication::restoreOverrideCursor();
                return;
            }
        }
    }

    for(i=0;i<TimeseriesData.length();i++)
    {
        name  = ui->table_TimeseriesData->item(i,1)->text();
        color = ui->table_TimeseriesData->item(i,2)->text();
        unit  = ui->table_TimeseriesData->item(i,3)->text();
        plusX = ui->table_TimeseriesData->item(i,4)->text();
        plusY = ui->table_TimeseriesData->item(i,5)->text();
        javascript = "SetSeriesOptions("+QString::number(i)+",'"+name+"','"+color+"',"+unit+","+plusX+","+plusY+")";
        ui->timeseries_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    //Inform HTML on the number of data series
    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("allocateData("+
                                                           QString::number(TimeseriesData.length())+")");

    //Send locations to HTML side
    for(i=0;i<TimeseriesData[0].nstations;i++)
    {
        x = TimeseriesData[0].station[i].longitude;
        y = TimeseriesData[0].station[i].latitude;
        javascript = "SetMarkerLocations("+QString::number(i)+
                ","+QString::number(x)+","+
                QString::number(y)+",'"+
                TimeseriesData[0].station[i].StationName+"')";
        jsResponse = ui->timeseries_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    //Now, all data should be on the backend for plotting. Bombs away...
    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("AddToMap()");
    ui->MainTabs->setCurrentIndex(1);
    ui->subtab_timeseries->setCurrentIndex(1);
    delay(1);
    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");

    QApplication::restoreOverrideCursor();
}

void MainWindow::on_check_TimeseriesAllData_toggled(bool checked)
{
    ui->date_TimeseriesStartDate->setEnabled(!checked);
    ui->date_TimeseriesEndDate->setEnabled(!checked);
    return;
}

void MainWindow::on_button_fitTimeseries_clicked()
{
    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    return;
}

void MainWindow::on_button_plotTimeseriesStation_clicked()
{
    QString DataString, javascript;
    double units;

    //Get the marker ID from the page
    int markerID = ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("getMarker()").toInt();

    //Catch false marker number
    if(markerID==-1)return;

    //Format the data
    for(int j=0;j<TimeseriesData.length();j++)
    {
        units = ui->table_TimeseriesData->item(j,3)->text().toDouble();
        DataString = FormatTimeseriesString(TimeseriesData[j],markerID,units);
        javascript = "";
        javascript = "AddDataSeries("+QString::number(j)+",'"+DataString+"')";
        ui->timeseries_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    //Call the plotting routine in HighCharts
    ui->timeseries_map->page()->mainFrame()->evaluateJavaScript("PlotTimeseries()");
    return;
}
