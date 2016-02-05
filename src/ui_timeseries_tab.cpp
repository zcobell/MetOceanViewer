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

#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
#include <timeseries_add_data.h>
#include <user_timeseries.h>

//-------------------------------------------//
//Called when the user tries to save the
//timeseries image
//-------------------------------------------//
void MainWindow::on_button_saveTimeseriesImage_clicked()
{
    QString Filename;
    QString filter = "JPG (*.jpg *.jpeg)";
    QString TempString = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"JPG (*.jpg *.jpeg)",&filter);

    if(TempString==NULL)
        return;

    splitPath(TempString,Filename,PreviousDirectory);
    QFile TimeseriesOutput(TempString);
    QPixmap TimeseriesImage(ui->timeseries_map->size());
    ui->timeseries_map->render(&TimeseriesImage);
    TimeseriesOutput.open(QIODevice::WriteOnly);
    TimeseriesImage.save(&TimeseriesOutput,"JPG",100);
    TimeseriesOutput.close();
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the user checks the timeseries
//auto y axis box
//-------------------------------------------//
void MainWindow::on_check_TimeseriesYauto_toggled(bool checked)
{
    ui->spin_TimeseriesYmin->setEnabled(!checked);
    ui->spin_TimeseriesYmax->setEnabled(!checked);
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Adds a row to the table and reads the new
//data into the timeseries variable
//-------------------------------------------//
void MainWindow::on_button_TimeseriesAddRow_clicked()
{
    QPointer<add_imeds_data> AddWindow = new add_imeds_data(this);
    QColor CellColor;
    int ierr;

    int NumberOfRows = ui->table_TimeseriesData->rowCount();
    AddWindow->setModal(false);
    AddWindow->set_default_dialog_box_elements(NumberOfRows);

    int WindowStatus = AddWindow->exec();

    QApplication::setOverrideCursor(Qt::WaitCursor);

    if(WindowStatus == 1)
    {

        NumberOfRows = NumberOfRows+1;

        //...Update the table
        ui->table_TimeseriesData->setRowCount(NumberOfRows);
        ui->table_TimeseriesData->setItem(NumberOfRows-1,0,new QTableWidgetItem(AddWindow->InputFileName));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,1,new QTableWidgetItem(AddWindow->InputSeriesName));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,2,new QTableWidgetItem(AddWindow->InputColorString));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,3,new QTableWidgetItem(QString::number(AddWindow->UnitConversion)));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,4,new QTableWidgetItem(QString::number(AddWindow->xadjust)));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,5,new QTableWidgetItem(QString::number(AddWindow->yadjust)));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,6,new QTableWidgetItem(AddWindow->InputFilePath));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,7,
                                     new QTableWidgetItem(AddWindow->InputFileColdStart.toString("yyyy-MM-dd hh:mm:ss")));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,8,new QTableWidgetItem(AddWindow->InputFileType));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,9,new QTableWidgetItem(AddWindow->InputStationFile));
        ui->table_TimeseriesData->setItem(NumberOfRows-1,10,new QTableWidgetItem(AddWindow->StationFilePath));
        CellColor.setNamedColor(AddWindow->InputColorString);
        ui->table_TimeseriesData->item(NumberOfRows-1,2)->setBackgroundColor(CellColor);
        ui->table_TimeseriesData->item(NumberOfRows-1,0)->setCheckState(Qt::Checked);

        //Tooltips in table cells
        ui->table_TimeseriesData->item(NumberOfRows-1,0)->setToolTip(AddWindow->InputFilePath);
        ui->table_TimeseriesData->item(NumberOfRows-1,1)->setToolTip(AddWindow->InputSeriesName);
        ui->table_TimeseriesData->item(NumberOfRows-1,3)->setToolTip(QString::number(AddWindow->UnitConversion));
        ui->table_TimeseriesData->item(NumberOfRows-1,4)->setToolTip(QString::number(AddWindow->xadjust));
        ui->table_TimeseriesData->item(NumberOfRows-1,5)->setToolTip(QString::number(AddWindow->yadjust));

    }

    QApplication::restoreOverrideCursor();
    AddWindow->close();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Called when the delete row button is
//clicked. Removes from the
//table as well as the data vector
//-------------------------------------------//
void MainWindow::on_button_TimeseriesDeleteRow_clicked()
{
    if(ui->table_TimeseriesData->rowCount()==0)
    {
        QMessageBox::critical(this,"ERROR","There are no datasets currently.");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    ui->table_TimeseriesData->removeRow(ui->table_TimeseriesData->currentRow());

    QApplication::restoreOverrideCursor();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Set up the table of time series files
//-------------------------------------------//
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
//-------------------------------------------//


//-------------------------------------------//
//Called when the edit row button is clicked.
//Updates the timeseries data vector
//-------------------------------------------//
void MainWindow::on_button_TimeseriesEditRow_clicked()
{
    int ierr, CurrentRow;
    double xadjust,yadjust,UnitConversion;
    QColor CellColor;
    QString Filename,Filepath,SeriesName,FileType,StationFilePath;
    QDateTime ColdStart;
    Qt::CheckState CheckState;
    QPointer<add_imeds_data> AddWindow = new add_imeds_data(this);

    if(ui->table_TimeseriesData->rowCount() == 0)
    {
        QMessageBox::critical(this,"ERROR","Insert a dataset first.");
        return;
    }

    if(ui->table_TimeseriesData->currentRow() == -1)
    {
        QMessageBox::critical(this,"ERROR","No dataset selected.");
        return;
    }

    AddWindow->setModal(false);
    AddWindow->EditBox = true;
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
    CheckState = ui->table_TimeseriesData->item(CurrentRow,0)->checkState();

    AddWindow->set_dialog_box_elements(Filename,Filepath,SeriesName,
                                        UnitConversion,xadjust,yadjust,
                                        CellColor,ColdStart,FileType,StationFilePath);

    int WindowStatus = AddWindow->exec();

    if(WindowStatus == 1)
    {
        ui->table_TimeseriesData->setItem(CurrentRow,0,new QTableWidgetItem(AddWindow->InputFileName));
        ui->table_TimeseriesData->setItem(CurrentRow,1,new QTableWidgetItem(AddWindow->InputSeriesName));
        ui->table_TimeseriesData->setItem(CurrentRow,3,new QTableWidgetItem(QString::number(AddWindow->UnitConversion)));
        ui->table_TimeseriesData->setItem(CurrentRow,4,new QTableWidgetItem(QString::number(AddWindow->xadjust)));
        ui->table_TimeseriesData->setItem(CurrentRow,5,new QTableWidgetItem(QString::number(AddWindow->yadjust)));
        ui->table_TimeseriesData->setItem(CurrentRow,6,new QTableWidgetItem(AddWindow->InputFilePath));
        ui->table_TimeseriesData->setItem(CurrentRow,7,new QTableWidgetItem(
                                         AddWindow->InputFileColdStart.toString("yyyy-MM-dd hh:mm:ss")));
        ui->table_TimeseriesData->setItem(CurrentRow,8,new QTableWidgetItem(AddWindow->InputFileType));
        ui->table_TimeseriesData->setItem(CurrentRow,9,new QTableWidgetItem(AddWindow->InputStationFile));
        ui->table_TimeseriesData->setItem(CurrentRow,10,new QTableWidgetItem(AddWindow->StationFilePath));

        //Tooltips in table cells
        ui->table_TimeseriesData->item(CurrentRow,0)->setToolTip(AddWindow->InputFilePath);
        ui->table_TimeseriesData->item(CurrentRow,1)->setToolTip(AddWindow->InputSeriesName);
        ui->table_TimeseriesData->item(CurrentRow,3)->setToolTip(QString::number(AddWindow->UnitConversion));
        ui->table_TimeseriesData->item(CurrentRow,4)->setToolTip(QString::number(AddWindow->xadjust));
        ui->table_TimeseriesData->item(CurrentRow,5)->setToolTip(QString::number(AddWindow->yadjust));
        ui->table_TimeseriesData->item(CurrentRow,0)->setCheckState(CheckState);

        ui->table_TimeseriesData->setItem(CurrentRow,2,new QTableWidgetItem(AddWindow->InputColorString));
        CellColor.setNamedColor(AddWindow->InputColorString);
        ui->table_TimeseriesData->item(CurrentRow,2)->setBackgroundColor(CellColor);

    }
    AddWindow->close();
    return;
}
//-------------------------------------------//



//-------------------------------------------//
//Send the data to the HTML side of the code
//for plotting
//-------------------------------------------//
void MainWindow::on_button_processTimeseriesData_clicked()
{
    int ierr;

    //Change the mouse pointer
    QApplication::setOverrideCursor(Qt::WaitCursor);

    if(!thisTimeseries.isNull())
        delete thisTimeseries;

    thisTimeseries = new user_timeseries(ui->table_TimeseriesData,
                                           ui->check_TimeseriesAllData,
                                           ui->check_TimeseriesYauto,
                                           ui->date_TimeseriesStartDate,
                                           ui->date_TimeseriesEndDate,
                                           ui->spin_TimeseriesYmin,
                                           ui->spin_TimeseriesYmax,
                                           ui->text_TimeseriesPlotTitle,
                                           ui->text_TimeseriesXaxisLabel,
                                           ui->text_TimeseriesYaxisLabel,
                                           ui->timeseries_map,
                                           ui->timeseries_graphics);

    ierr = thisTimeseries->processData();
    if(ierr!=0)
        QMessageBox::critical(this,"ERROR",thisTimeseries->getErrorString());
    else
    {
        ui->MainTabs->setCurrentIndex(1);
        ui->subtab_timeseries->setCurrentIndex(1);
        ui->timeseries_map->page()->runJavaScript("fitMarkers()");
    }

    QApplication::restoreOverrideCursor();
}
//-------------------------------------------//


//-------------------------------------------//
//Toggle the enable/disabled states for options
//that are not available when using all or a
//specific set of time series dates
//-------------------------------------------//
void MainWindow::on_check_TimeseriesAllData_toggled(bool checked)
{
    ui->date_TimeseriesStartDate->setEnabled(!checked);
    ui->date_TimeseriesEndDate->setEnabled(!checked);
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//A button to fit the time series locations
//to the viewport
//-------------------------------------------//
void MainWindow::on_button_fitTimeseries_clicked()
{
    ui->timeseries_map->page()->runJavaScript("fitMarkers()");
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Function called when the button to plot time
//series data is clicked or the enter/return
//key is pressed
//-------------------------------------------//
void MainWindow::on_button_plotTimeseriesStation_clicked()
{

    //Get the marker ID from the page
    int markerID = thisTimeseries->getClickedMarkerID();

    //Catch false marker number
    if(markerID==-1)return;

    thisTimeseries->plotData();

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Moves a table row up
//-------------------------------------------//
void MainWindow::on_button_moveRowUp_clicked()
{
    int currentRow = ui->table_TimeseriesData->currentRow();

    if(currentRow<1)
        return;

    //...Grab the rows
    QList<QTableWidgetItem*> row1 = grabTableRow(currentRow);
    QList<QTableWidgetItem*> row2 = grabTableRow(currentRow-1);

    //...Move the rows
    setTableRow(currentRow,row2);
    setTableRow(currentRow-1,row1);

    //...Set the selection
    ui->table_TimeseriesData->selectRow(currentRow-1);

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Move a table row down
//-------------------------------------------//
void MainWindow::on_button_moveRowDown_clicked()
{
    int currentRow = ui->table_TimeseriesData->currentRow();

    if(currentRow==ui->table_TimeseriesData->rowCount()-1)
        return;

    //...Grab the rows
    QList<QTableWidgetItem*> row1 = grabTableRow(currentRow);
    QList<QTableWidgetItem*> row2 = grabTableRow(currentRow+1);

    //...Move the rows
    setTableRow(currentRow,row2);
    setTableRow(currentRow+1,row1);

    //...Set the selection
    ui->table_TimeseriesData->selectRow(currentRow+1);

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Function that grabs an entire table row
//-------------------------------------------//
QList<QTableWidgetItem*> MainWindow::grabTableRow(int row)
{
    QList<QTableWidgetItem*> rowItems;
    for (int col = 0; col < ui->table_TimeseriesData->columnCount(); ++col)
        rowItems << ui->table_TimeseriesData->takeItem(row, col);
    return rowItems;
}
//-------------------------------------------//


//-------------------------------------------//
//Function that sets a table row based upon
//an input list
//-------------------------------------------//
void MainWindow::setTableRow(int row, const QList<QTableWidgetItem*>& rowItems)
{
    for (int col = 0; col < ui->table_TimeseriesData->columnCount(); ++col)
        ui->table_TimeseriesData->setItem(row, col, rowItems.at(col));
    return;
}
//-------------------------------------------//
