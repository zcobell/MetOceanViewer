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

//Called when the all date range imeds check box is toggled
void MainWindow::on_check_imedsalldata_toggled(bool checked)
{
    ui->date_imedsstart->setEnabled(!checked);
    ui->date_imedsend->setEnabled(!checked);
}


//Called when the process IMEDS button is clicked
void MainWindow::on_button_processIMEDS_clicked()
{
    bool ReadData1 = false;
    bool ReadData2 = false;
    int i;
    double ymin,ymax;
    QString ADCLabel,OBSLabel,PlotTitle,xlabel,ylabel,AutoY,GlobalData;
    QString OBSColorCode,ADCColorCode;

    this->setCursor(Qt::WaitCursor);

    //Set up the global data on the page
    ADCLabel = ui->text_adcirclabel->text();
    OBSLabel = ui->text_imedslabel->text();
    ADCColorCode = ADCIRCIMEDSColor.name();
    OBSColorCode = OBSIMEDSColor.name();
    PlotTitle = ui->text_imedsplottitle->text();
    xlabel = ui->text_xaxislabel->text();
    ylabel = ui->text_yaxislabel->text();
    ymin = ui->spin_imedsymin->value();
    ymax = ui->spin_imedsymax->value();

    if(ADCLabel.length()==0)ADCLabel = "Series 1";
    if(OBSLabel.length()==0)OBSLabel = "Series 2";
    if(PlotTitle.length()==0)PlotTitle = "IMEDS Station Data";
    if(xlabel.length()==0)xlabel = "Elevation";
    if(ylabel.length()==0)ylabel = "Date";

    if(ui->check_imedyauto->isChecked())
        AutoY="auto";
    else
        AutoY="set";

    GlobalData = "SetGlobalValues('"+PlotTitle+"','"+AutoY+"',"+
            QString::number(ymin)+","+QString::number(ymax)+
            ",'"+xlabel+"','"+ylabel+"','"+ADCLabel+"','"+OBSLabel+"','"+ADCColorCode+"','"+
            OBSColorCode+"')";
    ui->imeds_map->page()->mainFrame()->evaluateJavaScript(GlobalData);

    if(ADCIMEDSFile!=NULL && ui->check_ADCIMEDS->checkState()==2)
    {
        if(ADCIMEDS.success)
        {
            ReadData1 = true;
        }
        else
        {
            //ui->textbox_IMEDSstatus->appendPlainText("ERROR processing IMEDS Data File 1!");
            return;
        }
    }
    if(OBSIMEDSFile!=NULL && ui->check_OBSIMEDS->checkState()==2)
    {
        if(OBSIMEDS.success)
        {
            ReadData2 = true;
        }
        else
        {
            //ui->textbox_IMEDSstatus->appendPlainText("ERROR processing IMEDS Data File 2!");
            return;
        }
    }

    if(ReadData1 && ReadData2)
    {
        if(OBSIMEDS.nstations!=ADCIMEDS.nstations)
        {
            //ui->textbox_IMEDSstatus->appendPlainText("ERROR: Datasets are not equivelent!\n\n");
            return;
        }
        for(i=0;i<OBSIMEDS.nstations;i++)
        {
            if(OBSIMEDS.station[i].latitude!=ADCIMEDS.station[i].latitude &&
                    OBSIMEDS.station[i].longitude!=ADCIMEDS.station[i].longitude)
            {
                //ui->textbox_IMEDSstatus->appendPlainText(
                //            "ERROR: Station Index "+QString::number(i+1)+
                //            " not at same location in both files!");
                return;
            }
        }
        addIMEDSandADCIRCMarker(OBSIMEDS,ADCIMEDS);
    }
    else if(ReadData1 && !ReadData2)
    {
        addADCIRCMarker(ADCIMEDS);
    }
    else if(ReadData2 && !ReadData1)
    {
        addIMEDSMarker(OBSIMEDS);
    }
    this->setCursor(Qt::ArrowCursor);

}

//Called when the browse observation IMEDS data button is clicked
void MainWindow::on_browse_OBSIMEDS_clicked()
{
    QDateTime TempStartDate,TempEndDate;
    OBSIMEDSFile = QFileDialog::getOpenFileName(this,"Select Observation IMEDS File",
                                                PreviousDirectory,"IMEDS File (*.imeds *.IMEDS) ;; All Files (*.*)");
    if(OBSIMEDSFile!=NULL)
    {
        GetLeadingPath(OBSIMEDSFile);
        this->setCursor(Qt::WaitCursor);
        ui->text_OBSIMEDS->setText(RemoveLeadingPath(OBSIMEDSFile));
        ui->text_OBSIMEDS->setToolTip(OBSIMEDSFile);
        ui->button_processIMEDS->setEnabled(true);
        OBSIMEDS = readIMEDS(OBSIMEDSFile);
        getGlobalStartEndTime(OBSIMEDS,TempStartDate,TempEndDate);
        if(TempStartDate.operator <(IMEDSMinDate))
            IMEDSMinDate = TempStartDate;
        if(TempEndDate.operator >(IMEDSMaxDate))
            IMEDSMaxDate = TempEndDate;
        ui->date_imedsstart->setDateTime(IMEDSMinDate);
        ui->date_imedsend->setDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsend->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMinimumDateTime(IMEDSMinDate);
        ui->date_imedsend->setMinimumDateTime(IMEDSMinDate);
        this->setCursor(Qt::ArrowCursor);
    }
}

//Called when the browse adcirc IMEDS data button is clicked
void MainWindow::on_browse_ADCIMEDS_clicked()
{
    QDateTime TempStartDate,TempEndDate;
    ADCIMEDSFile = QFileDialog::getOpenFileName(this,"Select ADCIRC IMEDS File",
                                                PreviousDirectory,
                                                "IMEDS File (*.imeds *.IMEDS) ;; All Files (*.*)");
    if(ADCIMEDSFile!=NULL)
    {
        GetLeadingPath(ADCIMEDSFile);
        this->setCursor(Qt::WaitCursor);
        ui->text_ADCIMEDS->setText(RemoveLeadingPath(ADCIMEDSFile));
        ui->text_ADCIMEDS->setToolTip(ADCIMEDSFile);
        ui->button_processIMEDS->setEnabled(true);
        ADCIMEDS = readIMEDS(ADCIMEDSFile);
        getGlobalStartEndTime(ADCIMEDS,TempStartDate,TempEndDate);
        if(TempStartDate.operator <(IMEDSMinDate))
            IMEDSMinDate = TempStartDate;
        if(TempEndDate.operator >(IMEDSMaxDate))
            IMEDSMaxDate = TempEndDate;
        ui->date_imedsstart->setDateTime(IMEDSMinDate);
        ui->date_imedsend->setDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsend->setMaximumDateTime(IMEDSMaxDate);
        ui->date_imedsstart->setMinimumDateTime(IMEDSMinDate);
        ui->date_imedsend->setMinimumDateTime(IMEDSMinDate);
        this->setCursor(Qt::ArrowCursor);
    }

}

//Called when the observation imeds check box is toggeled
void MainWindow::on_check_OBSIMEDS_toggled(bool checked)
{
    ui->browse_OBSIMEDS->setEnabled(checked);
    ui->text_OBSIMEDS->setEnabled(checked);
    ui->button_imedsselectcolor->setEnabled(checked);
    ui->label_imedslabel->setEnabled(checked);
    ui->text_imedslabel->setEnabled(checked);
    ui->label_imedsselectcolor->setEnabled(checked);
    if(!checked && !(ui->check_ADCIMEDS->checkState()==2))
    {
        ui->button_processIMEDS->setEnabled(false);
    }
    else if(checked && ui->text_OBSIMEDS->text()!=NULL)
    {
        ui->button_processIMEDS->setEnabled(true);
    }
}

//Called when the adcirc imeds check box is toggled
void MainWindow::on_check_ADCIMEDS_toggled(bool checked)
{
    ui->browse_ADCIMEDS->setEnabled(checked);
    ui->text_ADCIMEDS->setEnabled(checked);
    ui->button_adcselectcolor->setEnabled(checked);
    ui->label_adcirclabel->setEnabled(checked);
    ui->text_adcirclabel->setEnabled(checked);
    ui->label_adcselectcolor->setEnabled(checked);
    if(!checked && !(ui->check_OBSIMEDS->checkState()==2))
    {
        ui->button_processIMEDS->setEnabled(false);
    }
    else if(checked && ui->text_ADCIMEDS->text()!=NULL)
    {
        ui->button_processIMEDS->setEnabled(true);
    }
}


void MainWindow::on_button_adcselectcolor_clicked()
{
    QString ButtonStyle;
    QColor TempColor;
    TempColor = QColorDialog::getColor(ADCIRCIMEDSColor);
    if(TempColor.isValid())
        ADCIRCIMEDSColor = TempColor;
    else
        return;

    ButtonStyle = MakeColorString(ADCIRCIMEDSColor);
    ui->button_adcselectcolor->setStyleSheet(ButtonStyle);
    ui->button_adcselectcolor->update();
    return;
}

void MainWindow::on_button_imedsselectcolor_clicked()
{
    QString ButtonStyle;
    QColor TempColor;

    TempColor = QColorDialog::getColor(OBSIMEDSColor);
    if(TempColor.isValid())
        OBSIMEDSColor = TempColor;
    else
        return;

    ButtonStyle = MakeColorString(OBSIMEDSColor);
    ui->button_imedsselectcolor->setStyleSheet(ButtonStyle);
    ui->button_imedsselectcolor->update();
    return;
}

//Adds a row to the table and reads the new data into the IMEDS variable
void MainWindow::on_button_addrow_clicked()
{
    add_imeds_data AddWindow;
    QAbstractItemModel *TableModel = ui->table_IMEDSData->model();
    QColor CellColor;
    int NumberOfRows = TableModel->rowCount();
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

    }
    AddWindow.close();
    return;
}

//Called when the delete row button is clicked. Removes from the
//table as well as the data vector
void MainWindow::on_button_deleterow_clicked()
{
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
        ui->table_IMEDSData->setItem(CurrentRow,2,new QTableWidgetItem(InputColorString));
        ui->table_IMEDSData->setItem(CurrentRow,3,new QTableWidgetItem(InputFilePath));
        CellColor.setNamedColor(InputColorString);
        ui->table_IMEDSData->item(CurrentRow,2)->setBackgroundColor(CellColor);

        //If we need to, read the new IMEDS file into the appropriate slot
        if(InputFilePath!=Filepath)
        {
            IMEDSData.remove(CurrentRow);
            IMEDSData.insert(CurrentRow,readIMEDS(InputFilePath));
            UpdateIMEDSDateRange(IMEDSData[CurrentRow]);
        }

    }
    AddWindow.close();
    return;
}
