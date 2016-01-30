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

#include <timeseries_add_data.h>
#include <ui_timeseries_add_data.h>
#include <MetOceanViewer.h>

//-------------------------------------------//
//A bunch of variables that we'll just
//define here because it is easy for the
//old FORTRAN programmer in me that likes
//global variables
//-------------------------------------------//
int NumIMEDSFiles = 0;
int CurrentRowsInTable = 0;
bool ColorUpdated, FileReadError;
bool EditBox;
double UnitConversion, xadjust, yadjust;
QColor RandomButtonColor;
QString InputFileName,InputColorString;
QString InputSeriesName,InputFilePath;
QString StationFilePath,InputFileType;
QString InputStationFile,CurrentFileName;
QDateTime InputFileColdStart;
//-------------------------------------------//


//-------------------------------------------//
//This brings up the dialog box used to add
//a file to the table of files
//-------------------------------------------//
add_imeds_data::add_imeds_data(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::add_imeds_data)
{
    ui->setupUi(this);
    ui->text_unitconvert->setValidator(new QDoubleValidator(this));
    ui->text_xadjust->setValidator(new QDoubleValidator(this));
    ui->text_yadjust->setValidator(new QDoubleValidator(this));
    PreviousDirectory = ((MainWindow *)parent)->PreviousDirectory;
}
//-------------------------------------------//


//-------------------------------------------//
//Destructor routine for the add imeds window
//-------------------------------------------//
add_imeds_data::~add_imeds_data()
{
    delete ui;
}
//-------------------------------------------//


//-------------------------------------------//
//Set the default elements with a series name,
//a blank filename, and a randomly generated color
//-------------------------------------------//
void add_imeds_data::set_default_dialog_box_elements(int NumRowsInTable)
{
    QString ButtonStyle;
    InputFileColdStart.setTimeSpec(Qt::UTC);
    ui->text_seriesname->setText("Series "+QString::number(NumRowsInTable+1));
    ui->text_unitconvert->setText("1.0");
    ui->text_xadjust->setText("0.0");
    ui->text_yadjust->setText("0.0");
    ui->date_coldstart->setDateTime(QDateTime::currentDateTime());
    RandomButtonColor = GenerateRandomColor();
    ButtonStyle = MakeColorString(RandomButtonColor);
    ui->button_IMEDSColor->setStyleSheet(ButtonStyle);
    ui->button_IMEDSColor->update();
    CurrentFileName = QString();
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//When editing an existing row, set the dialog
//box elements that we're bringing up for the user
//-------------------------------------------//
void add_imeds_data::set_dialog_box_elements(QString Filename, QString Filepath,
                                             QString SeriesName, double UnitConvert,
                                             double xmove, double ymove, QColor Color,
                                             QDateTime ColdStart, QString FileType,
                                             QString StationPath)
{
    QString ButtonStyle,StationFile;
    InputFileColdStart.setTimeSpec(Qt::UTC);
    splitPath(StationPath,StationFile,PreviousDirectory);
    ui->text_seriesname->setText(SeriesName);
    ui->text_filename->setText(Filename);
    ui->text_unitconvert->setText(QString::number(UnitConvert));
    ui->text_xadjust->setText(QString::number(xmove));
    ui->text_yadjust->setText(QString::number(ymove));
    ui->text_filetype->setText(FileType);
    ui->date_coldstart->setDateTime(ColdStart);
    ui->text_stationfile->setText(StationFile);
    InputFilePath = Filepath;
    CurrentFileName = Filepath;
    StationFilePath = StationPath;
    ButtonStyle = MakeColorString(Color);
    RandomButtonColor = Color;
    ui->button_IMEDSColor->setStyleSheet(ButtonStyle);
    ui->button_IMEDSColor->update();

    if(FileType == "IMEDS")
    {
        InputFileType = "IMEDS";
        ui->text_filetype->setText("IMEDS");
        ui->date_coldstart->setEnabled(false);
        ui->text_stationfile->setEnabled(false);
        ui->browse_stationfile->setEnabled(false);
        FileReadError = false;
    }
    else if(FileType == "NETCDF")
    {
        FileType = "NETCDF";
        ui->text_filetype->setText("NetCDF");
        ui->date_coldstart->setEnabled(true);
        ui->text_stationfile->setEnabled(false);
        ui->browse_stationfile->setEnabled(false);
        FileReadError = false;
    }
    else if(FileType == "ADCIRC")
    {
        ui->text_filetype->setText("ADCIRC");
        ui->date_coldstart->setEnabled(true);
        ui->text_stationfile->setEnabled(true);
        ui->browse_stationfile->setEnabled(true);
        FileReadError = false;
    }
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up the browse for file dialog
//-------------------------------------------//
void add_imeds_data::on_browse_filebrowse_clicked()
{
    QStringList List;
    QString Directory,filename,TempFile;

    if(EditBox)
        splitPath(InputFilePath,filename,Directory);
    else
        Directory = PreviousDirectory;

    QString TempPath = QFileDialog::getOpenFileName(this,"Select Observation IMEDS File",
            Directory,
            QString("MetOcean Viewer Compatible file (*.imeds *.61 *.62 *.71 *.72 *.nc) ;; ")+
            QString("IMEDS File (*.imeds *.IMEDS) ;; NetCDF ADCIRC Output Files (*.nc) ;; ")+
            QString("ADCIRC Output Files (*.61 *.62 *.71 *.72) ;; All Files (*.*)"));

    InputFilePath = TempPath;
    if(TempPath!=NULL || (TempPath==NULL && CurrentFileName!=NULL) )
    {

        if(TempPath==NULL)
        {
            TempPath = CurrentFileName;
            InputFilePath = CurrentFileName;
        }
        else
            CurrentFileName = TempPath;

        splitPath(TempPath,TempFile,PreviousDirectory);
        ui->text_filename->setText(TempFile);

        FileReadError = false;

        List = TempFile.split(".");
        InputFileType = List.value(List.length()-1).toUpper();

        if(InputFileType == "IMEDS")
        {
            InputFileType = "IMEDS";
            ui->text_filetype->setText("IMEDS");
            ui->date_coldstart->setEnabled(false);
            ui->text_stationfile->setEnabled(false);
            ui->browse_stationfile->setEnabled(false);
            FileReadError = false;
        }
        else if(InputFileType == "NC")
        {
            InputFileType = "NETCDF";
            ui->text_filetype->setText("NetCDF");
            ui->date_coldstart->setEnabled(true);
            ui->text_stationfile->setEnabled(false);
            ui->browse_stationfile->setEnabled(false);
            FileReadError = false;
        }
        else if(InputFileType == "61" || InputFileType == "62" || InputFileType == "71" || InputFileType == "72")
        {
            InputFileType = "ADCIRC";
            ui->text_filetype->setText("ADCIRC");
            ui->date_coldstart->setEnabled(true);
            ui->text_stationfile->setEnabled(true);
            ui->browse_stationfile->setEnabled(true);
            FileReadError = false;
        }
        else
        {
            FileReadError = true;
        }
    }
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Bring up a color palette and change the button color
//in the dialog when return comes
//-------------------------------------------//
void add_imeds_data::on_button_IMEDSColor_clicked()
{
    QColor TempColor = QColorDialog::getColor(RandomButtonColor);
    QString ButtonStyle;

    ColorUpdated = false;

    if(TempColor.isValid())
    {
        RandomButtonColor = TempColor;
        ColorUpdated = true;
        ButtonStyle = MakeColorString(RandomButtonColor);
        ui->button_IMEDSColor->setStyleSheet(ButtonStyle);
        ui->button_IMEDSColor->update();
    }

    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Browse dialog for a station file
//-------------------------------------------//
void add_imeds_data::on_browse_stationfile_clicked()
{
    QString TempFile;
    QString TempPath = QFileDialog::getOpenFileName(this,"Select ADCIRC Station File",
            PreviousDirectory,
            QString("Station Format Files (*.txt *.csv) ;; Text File (*.txt) ;; )")+
            QString("Comma Separated File (*.csv) ;; All Files (*.*)"));
    if(TempPath!=NULL)
    {
        StationFilePath = TempPath;
        splitPath(TempPath,TempFile,PreviousDirectory);
        ui->text_stationfile->setText(TempFile);
    }
    return;
}
//-------------------------------------------//


//-------------------------------------------//
//Redefine the local accept event with some validation
//of the dialog box
//-------------------------------------------//
void add_imeds_data::accept()
{

    QString TempString;

    InputFileName = ui->text_filename->text();
    InputColorString = RandomButtonColor.name();
    InputSeriesName = ui->text_seriesname->text();
    InputFileColdStart = ui->date_coldstart->dateTime();
    TempString = ui->text_unitconvert->text();
    InputStationFile = ui->text_stationfile->text();
    if(TempString==NULL)
        UnitConversion = 1.0;
    else
        UnitConversion = TempString.toDouble();

    TempString = ui->text_xadjust->text();
    if(TempString==NULL)
        xadjust = 0.0;
    else
        xadjust = TempString.toDouble();

    TempString = ui->text_yadjust->text();
    if(TempString==NULL)
        yadjust = 0.0;
    else
        yadjust = TempString.toDouble();

    if(InputFileName==NULL)
    {
        QMessageBox::critical(this,"ERROR","Please select an input file.");
        return;
    }
    else if(InputSeriesName==NULL)
    {
        QMessageBox::critical(this,"ERROR","Please input a series name.");
        return;
    }
    else if(InputColorString==NULL)
    {
        QMessageBox::critical(this,"ERROR","Please select a valid color for this series.");
        return;
    }
    else if(InputStationFile==NULL && InputFileType=="ADCIRC")
    {
        QMessageBox::critical(this,"ERROR","You did not select a station file.");
        return;
    }
    else
        QDialog::accept();
}
//-------------------------------------------//
