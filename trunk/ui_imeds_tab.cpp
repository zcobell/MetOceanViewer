//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
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
//  File: ui_imeds_tab.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"
#include "imeds.h"
#include "add_imeds_data.h"

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

    int NumberOfRows = ui->table_IMEDSData->rowCount();
    AddWindow.setModal(true);
    AddWindow.set_default_dialog_box_elements(NumberOfRows);

    int WindowStatus = AddWindow.exec();

    if(WindowStatus == 1)
    {
        //Verify the input is valid
        if(InputFileName==NULL)
        {
            QMessageBox::information(this,"ERROR","Please select an input file.");
            return;
        }
        if(InputSeriesName==NULL)
        {
            QMessageBox::information(this,"ERROR","Please input a series name.");
            return;
        }
        if(InputColorString==NULL)
        {
            QMessageBox::information(this,"ERROR","Please select a valid color for this series.");
            return;
        }

        //Ok, what we have is good, so populate
        NumberOfRows = NumberOfRows+1;
        IMEDSData.resize(NumberOfRows);
        IMEDSData[NumberOfRows-1] = readIMEDS(InputFilePath);
        UpdateIMEDSDateRange(IMEDSData[NumberOfRows-1]);
        ui->table_IMEDSData->setRowCount(NumberOfRows);
        ui->table_IMEDSData->setItem(NumberOfRows-1,0,new QTableWidgetItem(InputFileName));
        ui->table_IMEDSData->setItem(NumberOfRows-1,1,new QTableWidgetItem(InputSeriesName));
        ui->table_IMEDSData->setItem(NumberOfRows-1,2,new QTableWidgetItem(InputColorString));
        ui->table_IMEDSData->setItem(NumberOfRows-1,3,new QTableWidgetItem(InputFilePath));
        CellColor.setNamedColor(InputColorString);
        ui->table_IMEDSData->item(NumberOfRows-1,2)->setBackgroundColor(CellColor);

        if(!IMEDSData[NumberOfRows-1].success)
        {
            IMEDSData.remove(NumberOfRows-1);
            ui->table_IMEDSData->removeRow(NumberOfRows-1);
            QMessageBox::information(this,"ERROR","This IMEDS file could not be read correctly.");
            return;
        }

    }
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
    IMEDSData.remove(ui->table_IMEDSData->currentRow());
    ui->table_IMEDSData->removeRow(ui->table_IMEDSData->currentRow());
    return;
}

void MainWindow::SetupIMEDSTable()
{
    QString HeaderString = "Filename;Series Name;Color;FullPathToFile";
    QStringList Header = HeaderString.split(";");

    ui->table_IMEDSData->setRowCount(0);
    ui->table_IMEDSData->setColumnCount(4);
    ui->table_IMEDSData->setColumnHidden(3,true);
    ui->table_IMEDSData->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->table_IMEDSData->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    ui->table_IMEDSData->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    ui->table_IMEDSData->setHorizontalHeaderLabels(Header);
    ui->table_IMEDSData->setEditTriggers(QAbstractItemView::NoEditTriggers);

    return;
}

//Called when the edit row button is clicked. Updates the IMEDS data vector
void MainWindow::on_button_editrow_clicked()
{
    add_imeds_data AddWindow;
    QColor CellColor;
    QString Filename,Filepath,SeriesName;

    int CurrentRow;

    if(ui->table_IMEDSData->rowCount() == 0)
    {
        QMessageBox::information(this,"ERROR","Insert a row first.");
        return;
    }

    AddWindow.setModal(true);
    CurrentRow = ui->table_IMEDSData->currentRow();
    Filename = ui->table_IMEDSData->item(CurrentRow,0)->text();
    Filepath = ui->table_IMEDSData->item(CurrentRow,3)->text();
    SeriesName = ui->table_IMEDSData->item(CurrentRow,1)->text();
    CellColor.setNamedColor(ui->table_IMEDSData->item(CurrentRow,2)->text());

    AddWindow.set_dialog_box_elements(Filename,Filepath,SeriesName,CellColor);

    int WindowStatus = AddWindow.exec();

    if(WindowStatus == 1)
    {
        //Verify the input is valid
        if(InputFileName==NULL)
        {
            QMessageBox::information(this,"ERROR","Please select an input file.");
            return;
        }
        if(InputSeriesName==NULL)
        {
            QMessageBox::information(this,"ERROR","Please input a series name.");
            return;
        }
        if(InputColorString==NULL)
        {
            QMessageBox::information(this,"ERROR","Please select a valid color for this series.");
            return;
        }

        ui->table_IMEDSData->setItem(CurrentRow,0,new QTableWidgetItem(InputFileName));
        ui->table_IMEDSData->setItem(CurrentRow,1,new QTableWidgetItem(InputSeriesName));
        ui->table_IMEDSData->setItem(CurrentRow,3,new QTableWidgetItem(InputFilePath));

        if(ColorUpdated)
        {
            ui->table_IMEDSData->setItem(CurrentRow,2,new QTableWidgetItem(InputColorString));
            CellColor.setNamedColor(InputColorString);
            ui->table_IMEDSData->item(CurrentRow,2)->setBackgroundColor(CellColor);
        }

        //If we need to, read the new IMEDS file into the appropriate slot
        if(InputFilePath!=Filepath)
        {
            IMEDSData.remove(CurrentRow);
            IMEDSData.insert(CurrentRow,readIMEDS(InputFilePath));
            if(!IMEDSData[CurrentRow].success)
            {
                IMEDSData.remove(CurrentRow);
                ui->table_IMEDSData->removeRow(CurrentRow);
                QMessageBox::information(this,"ERROR","This IMEDS file could not be read correctly.");
                return;
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
    int i,j,ierr,total,count,pct;
    double x,y,YMin,YMax;
    bool Checked;
    QString javascript,DataString,name,color;
    QString PlotTitle,XLabel,YLabel,AutoY;
    QVariant jsResponse;

    if(ui->table_IMEDSData->rowCount()==0)
    {
        QMessageBox::information(this,"ERROR","There are no rows in the table.");
        return;
    }

    //Set up our axis labels and plot titles
    PlotTitle = ui->text_imedsplottitle->text();
    XLabel  = ui->text_xaxislabel->text();
    YLabel  = ui->text_yaxislabel->text();
    YMin    = ui->spin_imedsymin->value();
    YMax    = ui->spin_imedsymax->value();
    Checked = ui->check_imedyauto->isChecked();
    if(Checked)
        AutoY = "auto";
    else
        AutoY = "none";

    javascript = "setGlobal('"+PlotTitle+"','"+AutoY+"',"+YMin+","+YMax+",'"+XLabel+"','"+YLabel+"')";
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);

    total = IMEDSData.length()*IMEDSData[0].nstations;
    count = 0;

    //Verify that the stations are the same in all files
    if(IMEDSData.length()>1)
    {        
        for(i=1;i<IMEDSData.length();i++)
        {
            ierr = CheckStationLocationsIMEDS(IMEDSData[0],IMEDSData[i]);

            name  = ui->table_IMEDSData->item(0,1)->text();
            color = ui->table_IMEDSData->item(0,2)->text();
            javascript = "setSeriesOptions("+QString::number(i)+",'"+name+"','"+color+"')";
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);

            if(ierr==1)
            {
                QMessageBox::information(this,"ERROR","The station locations in the IMEDS files do not match.");
                return;
            }
        }
    }

    //Inform HTML on the number of data series
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("allocateData("+
                                                           QString::number(IMEDSData.length())+")");

    //Send locations to HTML side
    ui->progress_IMEDS->setValue(0);
    for(i=0;i<IMEDSData[0].nstations;i++)
    {
        x = IMEDSData[0].station[i].longitude;
        y = IMEDSData[0].station[i].latitude;
        javascript = "SetMarkerLocations("+QString::number(i)+
                ","+QString::number(x)+","+
                QString::number(y)+",'"+
                IMEDSData[0].station[i].StationName+"')";
        jsResponse = ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);

        javascript = "";
        //Send the data series to the back end as well
        for(j=0;j<IMEDSData.length();j++)
        {
            DataString = FormatIMEDSString(IMEDSData[j],i);
            javascript = "";
            javascript = "AddDataSeries("+QString::number(i)+","+QString::number(j)+",'"+DataString+"')";
            ui->imeds_map->page()->mainFrame()->evaluateJavaScript(javascript);
            count = count + 1;
            pct = static_cast<int>((static_cast<double>(count) / static_cast<double>(total))*100.0);
            ui->progress_IMEDS->setValue(pct);
        }
    }
    ui->progress_IMEDS->setValue(0);

    //Set the names and colors of the data series
    for(i=0;i<IMEDSData.length();i++)
    {
        name = ui->table_IMEDSData->item(i,1)->text();
        color = ui->table_IMEDSData->item(i,2)->text();
        ui->imeds_map->page()->mainFrame()->evaluateJavaScript("SetSeriesOptions("+QString::number(i)+",'"+name+"','"+color+"')");
    }

    //Now, all data should be on the backend for plotting. Bombs away...
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("AddToMap()");
    ui->MainTabs->setCurrentIndex(1);
    ui->subtab_IMEDS->setCurrentIndex(1);
    delay(1);
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript("fitMarkers()");
}

void MainWindow::on_check_imedsalldata_clicked(bool checked)
{
    ui->date_imedsstart->setEnabled(!checked);
    ui->date_imedsend->setEnabled(!checked);
    return;
}

