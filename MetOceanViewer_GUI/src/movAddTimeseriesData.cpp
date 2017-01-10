/*-------------------------------GPL-------------------------------------//
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
//-----------------------------------------------------------------------*/

#include <QMessageBox>
#include "mov_dialog_addtimeseriesdata.h"
#include "ui_mov_dialog_addtimeseries.h"
#include "mov_window_main.h"
#include "movColors.h"
#include "movGeneric.h"
#include "movFiletypes.h"

//-------------------------------------------//
//This brings up the dialog box used to add
//a file to the table of files
//-------------------------------------------//
mov_dialog_addtimeseries::mov_dialog_addtimeseries(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::mov_dialog_addtimeseries)
{
    ui->setupUi(this);
    ui->text_unitconvert->setValidator(new QDoubleValidator(this));
    ui->text_xadjust->setValidator(new QDoubleValidator(this));
    ui->text_yadjust->setValidator(new QDoubleValidator(this));
    this->PreviousDirectory = ((mov_window_main *)parent)->PreviousDirectory;
    this->epsgmap = this->proj->getMap();
}
//-------------------------------------------//


//-------------------------------------------//
//Destructor routine for the add imeds window
//-------------------------------------------//
mov_dialog_addtimeseries::~mov_dialog_addtimeseries()
{
    delete ui;
}
//-------------------------------------------//


//-------------------------------------------//
//Set the default elements with a series name,
//a blank filename, and a randomly generated color
//-------------------------------------------//
void mov_dialog_addtimeseries::set_default_dialog_box_elements(int NumRowsInTable)
{
    QString ButtonStyle;
    this->InputFileColdStart.setTimeSpec(Qt::UTC);
    ui->text_seriesname->setText("Series "+QString::number(NumRowsInTable+1));
    ui->text_unitconvert->setText("1.0");
    ui->text_xadjust->setText("0.0");
    ui->text_yadjust->setText("0.0");
    ui->date_coldstart->setDateTime(QDateTime::currentDateTime());
    this->InputFileColdStart = ui->date_coldstart->dateTime();
    this->RandomButtonColor = MovColors::GenerateRandomColor();
    this->InputColorString = MovColors::getHexColor(this->RandomButtonColor);
    ButtonStyle = MovColors::MakeColorString(this->RandomButtonColor);
    ui->button_seriesColor->setStyleSheet(ButtonStyle);
    ui->button_seriesColor->update();
    this->CurrentFileName = QString();
    this->epsg = 4326;
    this->proj = new proj4(this);
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//When editing an existing row, set the dialog
//box elements that we're bringing up for the user
//-------------------------------------------//
void mov_dialog_addtimeseries::set_dialog_box_elements(QString Filename, QString Filepath,
                                             QString SeriesName, double UnitConvert,
                                             double xmove, double ymove, QColor Color,
                                             QDateTime ColdStart, int FileType,
                                             QString StationPath, int epsg)
{
    QString ButtonStyle,StationFile;
    this->InputFileColdStart.setTimeSpec(Qt::UTC);
    MovGeneric::splitPath(StationPath,StationFile,this->PreviousDirectory);
    ui->text_seriesname->setText(SeriesName);
    ui->text_filename->setText(Filename);
    ui->text_unitconvert->setText(QString::number(UnitConvert));
    ui->text_xadjust->setText(QString::number(xmove));
    ui->text_yadjust->setText(QString::number(ymove));
    ui->text_filetype->setText(movFiletypes::integerFiletypeToString(FileType));
    ui->date_coldstart->setDateTime(ColdStart);
    ui->text_stationfile->setText(StationFile);
    this->InputFilePath = Filepath;
    this->CurrentFileName = Filepath;
    this->StationFilePath = StationPath;
    this->InputFileType = FileType;
    this->epsg = epsg;
    ButtonStyle = MovColors::MakeColorString(Color);
    this->RandomButtonColor = Color;
    ui->button_seriesColor->setStyleSheet(ButtonStyle);
    ui->button_seriesColor->update();

    if(FileType == FILETYPE_ASCII_IMEDS)
    {
        this->InputFileType = FileType;
        ui->text_filetype->setText("IMEDS");
        ui->date_coldstart->setEnabled(false);
        ui->text_stationfile->setEnabled(false);
        ui->browse_stationfile->setEnabled(false);
        this->FileReadError = false;
    }
    else if(FileType == FILETYPE_NETCDF_ADCIRC)
    {
        this->InputFileType = FileType;
        ui->text_filetype->setText("netCDF");
        ui->date_coldstart->setEnabled(true);
        ui->text_stationfile->setEnabled(false);
        ui->browse_stationfile->setEnabled(false);
        this->FileReadError = false;
    }
    else if(FileType == FILETYPE_ASCII_ADCIRC)
    {
        this->InputFileType = FileType;
        ui->text_filetype->setText("ADCIRC");
        ui->date_coldstart->setEnabled(true);
        ui->text_stationfile->setEnabled(true);
        ui->browse_stationfile->setEnabled(true);
        this->FileReadError = false;
    }
    else if(FileType == FILETYPE_NETCDF_DFLOW)
    {
        this->InputFileType = FileType;
        ui->text_filetype->setText("DFlow");
        ui->date_coldstart->setEnabled(false);
        ui->text_stationfile->setEnabled(false);
        ui->browse_stationfile->setEnabled(false);
        this->FileReadError = false;
    }
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up the browse for file dialog
//-------------------------------------------//
void mov_dialog_addtimeseries::on_browse_filebrowse_clicked()
{
    QStringList List;
    QString Directory,filename,TempFile;

    if(this->EditBox)
        MovGeneric::splitPath(this->InputFilePath,filename,Directory);
    else
        Directory = this->PreviousDirectory;

    QString TempPath = QFileDialog::getOpenFileName(this,"Select File",
            Directory,
            QString("MetOcean Viewer Compatible file (*.imeds *.61 *.62 *.71 *.72 *.nc) ;; ")+
            QString("IMEDS File (*.imeds *.IMEDS) ;; NetCDF ADCIRC Output Files (*.nc) ;; ")+
            QString("ADCIRC Output Files (*.61 *.62 *.71 *.72) ;; All Files (*.*)"));

    this->InputFilePath = TempPath;
    if(TempPath!=NULL || (TempPath==NULL && this->CurrentFileName!=NULL) )
    {

        if(TempPath==NULL)
        {
            TempPath = this->CurrentFileName;
            this->InputFilePath = this->CurrentFileName;
        }
        else
            this->CurrentFileName = TempPath;

        MovGeneric::splitPath(TempPath,TempFile,this->PreviousDirectory);
        ui->text_filename->setText(TempFile);

        this->FileReadError = false;
        this->InputFileType = movFiletypes::getIntegerFiletype(this->CurrentFileName);

        if(this->InputFileType == FILETYPE_ASCII_IMEDS)
        {
            ui->text_filetype->setText("IMEDS");
            ui->date_coldstart->setEnabled(false);
            ui->text_stationfile->setEnabled(false);
            ui->browse_stationfile->setEnabled(false);
            this->FileReadError = false;
        }
        else if(this->InputFileType == FILETYPE_NETCDF_ADCIRC)
        {
            ui->text_filetype->setText("netCDF");
            ui->date_coldstart->setEnabled(true);
            ui->text_stationfile->setEnabled(false);
            ui->browse_stationfile->setEnabled(false);
            this->FileReadError = false;
        }
        else if(this->InputFileType == FILETYPE_ASCII_ADCIRC)
        {
            ui->text_filetype->setText("ADCIRC");
            ui->date_coldstart->setEnabled(true);
            ui->text_stationfile->setEnabled(true);
            ui->browse_stationfile->setEnabled(true);
            this->FileReadError = false;
        }
        else if(this->InputFileType == FILETYPE_NETCDF_DFLOW)
        {
            ui->text_filetype->setText("DFlow");
            ui->date_coldstart->setEnabled(false);
            ui->text_stationfile->setEnabled(false);
            ui->browse_stationfile->setEnabled(false);
            this->FileReadError = false;
        }
        else
        {
            this->FileReadError = true;
        }
    }
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up a color palette and change the button color
//in the dialog when return comes
//-------------------------------------------//
void mov_dialog_addtimeseries::on_button_seriesColor_clicked()
{
    QColor TempColor = QColorDialog::getColor(this->RandomButtonColor);
    QString ButtonStyle;

    this->ColorUpdated = false;

    if(TempColor.isValid())
    {
        this->RandomButtonColor = TempColor;
        this->ColorUpdated = true;
        ButtonStyle = MovColors::MakeColorString(this->RandomButtonColor);
        ui->button_seriesColor->setStyleSheet(ButtonStyle);
        ui->button_seriesColor->update();
    }

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Browse dialog for a station file
//-------------------------------------------//
void mov_dialog_addtimeseries::on_browse_stationfile_clicked()
{
    QString TempFile;
    QString TempPath = QFileDialog::getOpenFileName(this,"Select ADCIRC Station File",
            this->PreviousDirectory,
            QString("Station Format Files (*.txt *.csv) ;; Text File (*.txt) ;; )")+
            QString("Comma Separated File (*.csv) ;; All Files (*.*)"));
    if(TempPath!=NULL)
    {
        this->StationFilePath = TempPath;
        MovGeneric::splitPath(TempPath,TempFile,this->PreviousDirectory);
        ui->text_stationfile->setText(TempFile);
    }
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Redefine the local accept event with some validation
//of the dialog box
//-------------------------------------------//
void mov_dialog_addtimeseries::accept()
{

    QString TempString;

    this->InputFileName = ui->text_filename->text();
    this->InputColorString = this->RandomButtonColor.name();
    this->InputSeriesName = ui->text_seriesname->text();
    this->InputFileColdStart = ui->date_coldstart->dateTime();
    this->epsg = ui->spin_epsg->value();
    TempString = ui->text_unitconvert->text();
    this->InputStationFile = ui->text_stationfile->text();
    if(TempString==NULL)
        this->UnitConversion = 1.0;
    else
        this->UnitConversion = TempString.toDouble();

    TempString = ui->text_xadjust->text();
    if(TempString==NULL)
        this->xadjust = 0.0;
    else
        this->xadjust = TempString.toDouble();

    //...Convert to other time units
    if(ui->combo_timeSelect->currentText()=="seconds")
        this->xadjust = this->xadjust / 3600;
    else if(ui->combo_timeSelect->currentText()=="minutes")
        this->xadjust = this->xadjust / 60;
    else if(ui->combo_timeSelect->currentText()=="days")
        this->xadjust = this->xadjust * 24;

    TempString = ui->text_yadjust->text();
    if(TempString==NULL)
        this->yadjust = 0.0;
    else
        this->yadjust = TempString.toDouble();

    if(this->InputFileName==NULL)
    {
        QMessageBox::critical(this,"ERROR","Please select an input file.");
        return;
    }
    else if(this->InputSeriesName==NULL)
    {
        QMessageBox::critical(this,"ERROR","Please input a series name.");
        return;
    }
    else if(this->InputColorString==NULL)
    {
        QMessageBox::critical(this,"ERROR","Please select a valid color for this series.");
        return;
    }
    else if(this->InputStationFile==NULL && this->InputFileType==FILETYPE_ASCII_ADCIRC)
    {
        QMessageBox::critical(this,"ERROR","You did not select a station file.");
        return;
    }
    else if(!this->epsgmap->contains(this->epsg))
    {
        QMessageBox::critical(this,"ERROR","You did not enter a valid EPSG coordinate system.");
    }
    else
        QDialog::accept();
}
//-------------------------------------------//


void mov_dialog_addtimeseries::on_button_presetColor1_clicked()
{
    this->ColorUpdated = true;
    ui->button_seriesColor->setStyleSheet(ui->button_presetColor1->styleSheet());
    this->RandomButtonColor = MovColors::styleSheetToColor(ui->button_seriesColor->styleSheet());
    ui->button_seriesColor->update();
}


void mov_dialog_addtimeseries::on_button_presetColor2_clicked()
{
    this->ColorUpdated = true;
    ui->button_seriesColor->setStyleSheet(ui->button_presetColor2->styleSheet());
    this->RandomButtonColor = MovColors::styleSheetToColor(ui->button_seriesColor->styleSheet());
    ui->button_seriesColor->update();
}


void mov_dialog_addtimeseries::on_button_presetColor3_clicked()
{
    this->ColorUpdated = true;
    ui->button_seriesColor->setStyleSheet(ui->button_presetColor3->styleSheet());
    this->RandomButtonColor = MovColors::styleSheetToColor(ui->button_seriesColor->styleSheet());
    ui->button_seriesColor->update();
}


void mov_dialog_addtimeseries::on_button_presetColor4_clicked()
{
    this->ColorUpdated = true;
    ui->button_seriesColor->setStyleSheet(ui->button_presetColor4->styleSheet());
    this->RandomButtonColor = MovColors::styleSheetToColor(ui->button_seriesColor->styleSheet());
    ui->button_seriesColor->update();
}


void mov_dialog_addtimeseries::on_button_describeepsg_clicked()
{
    if(this->epsgmap->contains(ui->spin_epsg->value()))
    {
        QString description = QString::fromStdString(this->epsgmap->value(ui->spin_epsg->value()));
        QMessageBox::information(this,"Coordinate System Description",description);
    }
    else
        QMessageBox::information(this,"Coordinate System Description","ERROR: Invalid EPSG");
}


void mov_dialog_addtimeseries::on_spin_epsg_valueChanged(int arg1)
{
    if(this->epsgmap->contains(arg1))
        ui->spin_epsg->setStyleSheet("background-color: rgb(255, 255, 255);");
    else
        ui->spin_epsg->setStyleSheet("background-color: rgb(255, 0, 0);");
    return;
}
