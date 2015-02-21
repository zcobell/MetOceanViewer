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
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: ui_timeseries_tab.cpp
//
//------------------------------------------------------------------------------

#include <ADCvalidator.h>
#include <ui_ADCvalidator_main.h>
#include <timeseries.h>
#include <timeseries_add_data.h>

QVector<IMEDS> IMEDSData;

//Called when the user tries to save the IMEDS image
void MainWindow::on_button_saveIMEDSImage_clicked()
{
    QString filter = "JPG (*.jpg *.jpeg)";
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(Filename==NULL)
        return;

    GetLeadingPath(Filename);
    QFile IMEDSOutput(Filename);
    QPixmap IMEDSImage(ui->imeds_map->size());
    ui->imeds_map->render(&IMEDSImage);
    IMEDSOutput.open(QIODevice::WriteOnly);
    IMEDSImage.save(&IMEDSOutput,"JPG",100);

}

//Called when the user checks the imeds auto y axis box
void MainWindow::on_check_imedyauto_toggled(bool checked)
{
    ui->spin_imedsymin->setEnabled(!checked);
    ui->spin_imedsymax->setEnabled(!checked);
}


//Adds a row to the table and reads the new data into the IMEDS variable
void MainWindow::on_button_addrow_clicked()
{
    add_imeds_data AddWindow;
    QColor CellColor;
    ADCNC NetCDFData;
    ADCASCII ADCData;

    EditBox = false;

    int NumberOfRows = ui->table_IMEDSData->rowCount();
    AddWindow.setModal(false);
    AddWindow.set_default_dialog_box_elements(NumberOfRows);

    int WindowStatus = AddWindow.exec();

    QApplication::setOverrideCursor(Qt::WaitCursor);

    if(WindowStatus == 1)
    {

        NumberOfRows = NumberOfRows+1;
        IMEDSData.resize(NumberOfRows);

        IMEDSData[NumberOfRows-1].success = false;

        if(InputFileType=="IMEDS")
        {
            IMEDSData[NumberOfRows-1] = readIMEDS(InputFilePath);
            UpdateIMEDSDateRange(IMEDSData[NumberOfRows-1]);
        }
        else if(InputFileType=="NETCDF")
        {
            NetCDFData = readADCIRCnetCDF(InputFilePath);
            if(!NetCDFData.success)
                IMEDSData[NumberOfRows-1].success = false;
            else
            {
                IMEDSData[NumberOfRows-1] = NetCDF_to_IMEDS(NetCDFData,InputFileColdStart);
                UpdateIMEDSDateRange(IMEDSData[NumberOfRows-1]);
            }
        }
        else if(InputFileType=="ADCIRC")
        {
            ADCData = readADCIRCascii(InputFilePath,StationFilePath);
            if(!ADCData.success)
                IMEDSData[NumberOfRows-1].success = false;
            else
            {
                IMEDSData[NumberOfRows-1] = ADCIRC_to_IMEDS(ADCData,InputFileColdStart);
                UpdateIMEDSDateRange(IMEDSData[NumberOfRows-1]);
            }
        }

        ui->table_IMEDSData->setRowCount(NumberOfRows);
        ui->table_IMEDSData->setItem(NumberOfRows-1,0,new QTableWidgetItem(InputFileName));
        ui->table_IMEDSData->setItem(NumberOfRows-1,1,new QTableWidgetItem(InputSeriesName));
        ui->table_IMEDSData->setItem(NumberOfRows-1,2,new QTableWidgetItem(InputColorString));
        ui->table_IMEDSData->setItem(NumberOfRows-1,3,new QTableWidgetItem(QString::number(UnitConversion)));
        ui->table_IMEDSData->setItem(NumberOfRows-1,4,new QTableWidgetItem(QString::number(xadjust)));
        ui->table_IMEDSData->setItem(NumberOfRows-1,5,new QTableWidgetItem(QString::number(yadjust)));
        ui->table_IMEDSData->setItem(NumberOfRows-1,6,new QTableWidgetItem(InputFilePath));
        ui->table_IMEDSData->setItem(NumberOfRows-1,7,new QTableWidgetItem(InputFileColdStart.toString("yyyy-MM-dd hh:mm:ss")));
        ui->table_IMEDSData->setItem(NumberOfRows-1,8,new QTableWidgetItem(InputFileType));
        ui->table_IMEDSData->setItem(NumberOfRows-1,9,new QTableWidgetItem(InputStationFile));
        ui->table_IMEDSData->setItem(NumberOfRows-1,10,new QTableWidgetItem(StationFilePath));
        CellColor.setNamedColor(InputColorString);
        ui->table_IMEDSData->item(NumberOfRows-1,2)->setBackgroundColor(CellColor);

        //Tooltips in table cells
        ui->table_IMEDSData->item(NumberOfRows-1,0)->setToolTip(InputFilePath);
        ui->table_IMEDSData->item(NumberOfRows-1,1)->setToolTip(InputSeriesName);
        ui->table_IMEDSData->item(NumberOfRows-1,3)->setToolTip(QString::number(UnitConversion));
        ui->table_IMEDSData->item(NumberOfRows-1,4)->setToolTip(QString::number(xadjust));
        ui->table_IMEDSData->item(NumberOfRows-1,5)->setToolTip(QString::number(yadjust));

        if(!IMEDSData[NumberOfRows-1].success)
        {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            IMEDSData.remove(NumberOfRows-1);
            ui->table_IMEDSData->removeRow(NumberOfRows-1);
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
void MainWindow::on_button_deleterow_clicked()
{
    if(ui->table_IMEDSData->rowCount()==0)
    {
        QMessageBox::information(this,"ERROR","There are no rows in the table.");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    //Reload the page so that arrays are refreshed.
    //This is sort of cheating, but it works
    ui->imeds_map->reload();
    delay(2);

    IMEDSData.remove(ui->table_IMEDSData->currentRow());
    ui->table_IMEDSData->removeRow(ui->table_IMEDSData->currentRow());

    QApplication::restoreOverrideCursor();

    return;
}

void MainWindow::SetupIMEDSTable()
{
    QString HeaderString = QString("Filename;Series Name;Color;Unit Conversion;")+
                           QString("x-shift;y-shift;FullPathToFile;Cold Start;")+
                           QString("FileType;StationFile;StationFilePath");
    QStringList Header = HeaderString.split(";");

    ui->table_IMEDSData->setRowCount(0);
    ui->table_IMEDSData->setColumnCount(11);
    ui->table_IMEDSData->setColumnHidden(6,true);
    ui->table_IMEDSData->setColumnHidden(7,true);
    ui->table_IMEDSData->setColumnHidden(8,true);
    ui->table_IMEDSData->setColumnHidden(9,true);
    ui->table_IMEDSData->setColumnHidden(10,true);
    ui->table_IMEDSData->setHorizontalHeaderLabels(Header);
    ui->table_IMEDSData->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->table_IMEDSData->setEditTriggers(QAbstractItemView::NoEditTriggers);

    return;
}

//Called when the edit row button is clicked. Updates the IMEDS data vector
void MainWindow::on_button_editrow_clicked()
{
    add_imeds_data AddWindow;
    QColor CellColor;
    QString Filename,Filepath,SeriesName,FileType;
    QDateTime ColdStart;

    int CurrentRow;

    EditBox = true;

    if(ui->table_IMEDSData->rowCount() == 0)
    {
        QMessageBox::information(this,"ERROR","Insert a row first.");
        return;
    }

    if(ui->table_IMEDSData->currentRow() == -1)
    {
        QMessageBox::information(this,"ERROR","No row selected.");
        return;
    }

    AddWindow.setModal(false);
    CurrentRow = ui->table_IMEDSData->currentRow();
    Filename = ui->table_IMEDSData->item(CurrentRow,0)->text();
    Filepath = ui->table_IMEDSData->item(CurrentRow,6)->text();
    SeriesName = ui->table_IMEDSData->item(CurrentRow,1)->text();
    UnitConversion = ui->table_IMEDSData->item(CurrentRow,3)->text().toDouble();
    xadjust = ui->table_IMEDSData->item(CurrentRow,4)->text().toDouble();
    yadjust = ui->table_IMEDSData->item(CurrentRow,5)->text().toDouble();
    FileType = ui->table_IMEDSData->item(CurrentRow,8)->text();
    ColdStart = QDateTime::fromString(ui->table_IMEDSData->item(CurrentRow,7)->text().simplified(),"yyyy-MM-dd hh:mm:ss");
    CellColor.setNamedColor(ui->table_IMEDSData->item(CurrentRow,2)->text());

    AddWindow.set_dialog_box_elements(Filename,Filepath,SeriesName,
                                      UnitConversion,xadjust,yadjust,
                                      CellColor,ColdStart,FileType,
                                      InputStationFile,StationFilePath);

    int WindowStatus = AddWindow.exec();

    if(WindowStatus == 1)
    {
        ui->table_IMEDSData->setItem(CurrentRow,0,new QTableWidgetItem(InputFileName));
        ui->table_IMEDSData->setItem(CurrentRow,1,new QTableWidgetItem(InputSeriesName));
        ui->table_IMEDSData->setItem(CurrentRow,3,new QTableWidgetItem(QString::number(UnitConversion)));
        ui->table_IMEDSData->setItem(CurrentRow,4,new QTableWidgetItem(QString::number(xadjust)));
        ui->table_IMEDSData->setItem(CurrentRow,5,new QTableWidgetItem(QString::number(yadjust)));
        ui->table_IMEDSData->setItem(CurrentRow,6,new QTableWidgetItem(InputFilePath));
        ui->table_IMEDSData->setItem(CurrentRow,7,new QTableWidgetItem(InputFileColdStart.toString("yyyy-MM-dd hh:mm:ss")));
        ui->table_IMEDSData->setItem(CurrentRow,8,new QTableWidgetItem(InputFileType));
        ui->table_IMEDSData->setItem(CurrentRow,9,new QTableWidgetItem(InputStationFile));
        ui->table_IMEDSData->setItem(CurrentRow,10,new QTableWidgetItem(StationFilePath));

        //Tooltips in table cells
        ui->table_IMEDSData->item(CurrentRow,0)->setToolTip(InputFilePath);
        ui->table_IMEDSData->item(CurrentRow,1)->setToolTip(InputSeriesName);
        ui->table_IMEDSData->item(CurrentRow,3)->setToolTip(QString::number(UnitConversion));
        ui->table_IMEDSData->item(CurrentRow,4)->setToolTip(QString::number(xadjust));
        ui->table_IMEDSData->item(CurrentRow,5)->setToolTip(QString::number(yadjust));

        if(ColorUpdated)
        {
            ui->table_IMEDSData->setItem(CurrentRow,2,new QTableWidgetItem(InputColorString));
            CellColor.setNamedColor(InputColorString);
            ui->table_IMEDSData->item(CurrentRow,2)->setBackgroundColor(CellColor);
        }

        //If we need to, read the new IMEDS file into the appropriate slot
        if(InputFilePath!=Filepath)
        {
            if(InputFileType=="IMEDS")
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                IMEDSData.remove(CurrentRow);
                IMEDSData.insert(CurrentRow,readIMEDS(InputFilePath));
                if(!IMEDSData[CurrentRow].success)
                {
                    IMEDSData.remove(CurrentRow);
                    ui->table_IMEDSData->removeRow(CurrentRow);
                    QApplication::restoreOverrideCursor();
                    QMessageBox::information(this,"ERROR","This IMEDS file could not be read correctly.");
                    return;
                }
                QApplication::restoreOverrideCursor();
            }
            else if(InputFileType=="NETCDF")
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                IMEDSData.remove(CurrentRow);
                IMEDSData.insert(CurrentRow,NetCDF_to_IMEDS(readADCIRCnetCDF(InputFilePath),InputFileColdStart));
                if(!IMEDSData[CurrentRow].success)
                {
                    IMEDSData.remove(CurrentRow);
                    ui->table_IMEDSData->removeRow(CurrentRow);
                    QApplication::restoreOverrideCursor();
                    QMessageBox::information(this,"ERROR","This NETCDF file could not be read correctly.");
                    return;
                }
                QApplication::restoreOverrideCursor();
            }
            else if(InputFileType=="ADCIRC")
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);
                IMEDSData.remove(CurrentRow);
                IMEDSData.insert(CurrentRow,
                    ADCIRC_to_IMEDS(readADCIRCascii(InputFilePath,StationFilePath),InputFileColdStart));
                if(!IMEDSData[CurrentRow].success)
                {
                    IMEDSData.remove(CurrentRow);
                    ui->table_IMEDSData->removeRow(CurrentRow);
                    QApplication::restoreOverrideCursor();
                    QMessageBox::information(this,"ERROR","This ADCIRC file could not be read correctly.");
                    return;
                }
                QApplication::restoreOverrideCursor();
            }
            UpdateIMEDSDateRange(IMEDSData[CurrentRow]);
        }

    }
    AddWindow.close();
    return;
}


//Send the data to the HTML side of the code for plotting
void MainWindow::on_button_processIMEDSData_clicked()
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
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("clearMarkers()");
    //ui->imeds_map->reload();
    delay(1);

    if(ui->table_IMEDSData->rowCount()==0)
    {
        QMessageBox::information(this,"ERROR","There are no rows in the table.");
        QApplication::restoreOverrideCursor();
        return;
    }

    //Set up our axis labels and plot titles
    PlotTitle = ui->text_imedsplottitle->text();
    XLabel  = ui->text_xaxislabel->text();
    YLabel  = ui->text_yaxislabel->text();
    YMin    = ui->spin_imedsymin->value();
    YMax    = ui->spin_imedsymax->value();
    XMin    = ui->date_imedsstart->dateTime().toString("yyyy-MM-dd");
    XMax    = ui->date_imedsend->dateTime().toString("yyyy-MM-dd");
    Checked = ui->check_imedyauto->isChecked();
    if(Checked)
        AutoY = "auto";
    else
        AutoY = "none";

    Checked = ui->check_imedsalldata->isChecked();
    if(Checked)
        AutoX = "auto";
    else
        AutoX = "none";

    javascript = "setGlobal('"+PlotTitle+"','"+AutoY+"',"+QString::number(YMin)+
            ","+QString::number(YMax)+",'"+XLabel+"','"+YLabel+"','"+AutoX+"','"+
            XMin+"','"+XMax+"')";

    ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);

    //Verify that the stations are the same in all files
    if(IMEDSData.length()>1)
    {        
        for(i=1;i<IMEDSData.length();i++)
        {
            ierr = CheckStationLocationsIMEDS(IMEDSData[0],IMEDSData[i]);
            if(ierr==1)
            {
                QMessageBox::information(this,"ERROR","The station locations in the IMEDS files do not match.");
                QApplication::restoreOverrideCursor();
                return;
            }
        }
    }

    for(i=0;i<IMEDSData.length();i++)
    {
        name  = ui->table_IMEDSData->item(i,1)->text();
        color = ui->table_IMEDSData->item(i,2)->text();
        unit  = ui->table_IMEDSData->item(i,3)->text();
        plusX = ui->table_IMEDSData->item(i,4)->text();
        plusY = ui->table_IMEDSData->item(i,5)->text();
        javascript = "SetSeriesOptions("+QString::number(i)+",'"+name+"','"+color+"',"+unit+","+plusX+","+plusY+")";
        ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    //Inform HTML on the number of data series
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("allocateData("+
                                                           QString::number(IMEDSData.length())+")");

    //Send locations to HTML side
    for(i=0;i<IMEDSData[0].nstations;i++)
    {
        x = IMEDSData[0].station[i].longitude;
        y = IMEDSData[0].station[i].latitude;
        javascript = "SetMarkerLocations("+QString::number(i)+
                ","+QString::number(x)+","+
                QString::number(y)+",'"+
                IMEDSData[0].station[i].StationName+"')";
        jsResponse = ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    //Now, all data should be on the backend for plotting. Bombs away...
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("AddToMap()");
    ui->MainTabs->setCurrentIndex(1);
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");

    QApplication::restoreOverrideCursor();
}

void MainWindow::on_check_imedsalldata_toggled(bool checked)
{
    ui->date_imedsstart->setEnabled(!checked);
    ui->date_imedsend->setEnabled(!checked);
    return;
}

void MainWindow::on_button_fitIMEDS_clicked()
{
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
    return;
}

void MainWindow::on_button_plotStation_clicked()
{
    QString DataString, javascript;
    double units;

    //Get the marker ID from the page
    int markerID = ui->imeds_map->page()->mainFrame()->evaluateJavaScript("getMarker()").toInt();

    //Catch false marker number
    if(markerID==-1)return;

    //Format the data
    for(int j=0;j<IMEDSData.length();j++)
    {
        units = ui->table_IMEDSData->item(j,3)->text().toDouble();
        DataString = FormatIMEDSString(IMEDSData[j],markerID,units);
        javascript = "";
        javascript = "AddDataSeries("+QString::number(j)+",'"+DataString+"')";
        ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);
    }

    //Call the plotting routine in HighCharts
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("PlotIMEDSSeries()");
    return;
}
