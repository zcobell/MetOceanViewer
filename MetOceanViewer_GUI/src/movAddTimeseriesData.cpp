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
#include "movDflow.h"
#include <QDebug>

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
    connect(this,SIGNAL(addTimeseriesError(QString)),this,SLOT(throwErrorMessageBox(QString)));
}
//-------------------------------------------//


void mov_dialog_addtimeseries::throwErrorMessageBox(QString errorString)
{
    QMessageBox::critical(this,"ERROR",errorString);
    return;
}


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
    this->dFlowVariable = QString();
    this->proj = new proj4(this);

    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);

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
                                             QString StationPath, int epsg, QString varname,
                                             int layer)
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
    this->dFlowVariable = varname;
    this->epsg = epsg;
    this->layer = layer;
    ButtonStyle = MovColors::MakeColorString(Color);
    this->RandomButtonColor = Color;
    ui->button_seriesColor->setStyleSheet(ButtonStyle);
    ui->button_seriesColor->update();

    this->setItemsByFiletype();

    return;
}
//-------------------------------------------//


void mov_dialog_addtimeseries::setItemsByFiletype()
{

    if(this->InputFileType == FILETYPE_ASCII_IMEDS)
    {
        ui->text_filetype->setText("IMEDS");
        this->setColdstartSelectElements(false);
        this->setStationSelectElements(false);
        this->setVariableSelectElements(false);
        this->setVerticalLayerElements(false);
        this->FileReadError = false;
    }
    else if(this->InputFileType == FILETYPE_NETCDF_ADCIRC)
    {
        ui->text_filetype->setText("netCDF");
        ui->date_coldstart->setEnabled(true);
        this->setColdstartSelectElements(true);
        this->setStationSelectElements(true);
        this->setVariableSelectElements(false);
        this->setVerticalLayerElements(false);
        this->FileReadError = false;
    }
    else if(this->InputFileType == FILETYPE_ASCII_ADCIRC)
    {
        ui->text_filetype->setText("ADCIRC");
        this->setColdstartSelectElements(true);
        this->setStationSelectElements(true);
        this->setVariableSelectElements(false);
        this->setVerticalLayerElements(false);
        this->FileReadError = false;
    }
    else if(this->InputFileType == FILETYPE_NETCDF_DFLOW)
    {
        QString variable = this->dFlowVariable;

        ui->text_filetype->setText("DFlow-FM");
        this->FileReadError = false;

        this->setColdstartSelectElements(false);
        this->setStationSelectElements(false);
        this->setVariableSelectElements(true);

        MovDflow *dflow = new MovDflow(this->InputFilePath,this);

        if(dflow->isError())
        {
            emit addTimeseriesError("Error reading DFlow-FM file");
            this->FileReadError = true;
            return;
        }

        if(dflow->is3d())
        {
            this->setVerticalLayerElements(true);
            ui->spin_layer->setMinimum(1);
            ui->spin_layer->setMaximum(dflow->getNumLayers());
            ui->spin_layer->setValue(this->layer);
        }
        else
            this->setVerticalLayerElements(false);

        ui->combo_variableSelect->clear();
        QStringList dflowVariables = dflow->getVaribleList();
        ui->combo_variableSelect->addItems(dflowVariables);

        if(dflowVariables.contains(variable))
            ui->combo_variableSelect->setCurrentIndex(dflowVariables.indexOf(variable));
        else
            ui->combo_variableSelect->setCurrentIndex(0);

        this->dFlowVariable = variable;
    }
    else
    {
        this->FileReadError = true;
        emit addTimeseriesError("No suitable filetype found.");
    }
    return;
}


void mov_dialog_addtimeseries::setVerticalLayerElements(bool enabled)
{
    ui->spin_layer->setEnabled(enabled);
    if(enabled)
    {
        ui->spin_layer->show();
        ui->label_layer->show();
    }
    else
    {
        ui->spin_layer->hide();
        ui->label_layer->hide();
    }
    return;
}


void mov_dialog_addtimeseries::setStationSelectElements(bool enabled)
{
    ui->browse_stationfile->setEnabled(enabled);
    ui->text_stationfile->setEnabled(enabled);
    if(enabled)
    {
        ui->browse_stationfile->show();
        ui->text_stationfile->show();
        ui->label_stationfile->show();
    }
    else
    {
        ui->browse_stationfile->hide();
        ui->text_stationfile->hide();
        ui->label_stationfile->hide();
    }
    return;
}

void mov_dialog_addtimeseries::setColdstartSelectElements(bool enabled)
{
    ui->date_coldstart->setEnabled(enabled);
    if(enabled)
    {
        ui->date_coldstart->show();
        ui->label_coldstart->show();
    }
    else
    {
        ui->date_coldstart->hide();
        ui->label_coldstart->hide();
    }
    return;
}


void mov_dialog_addtimeseries::setVariableSelectElements(bool enabled)
{
    ui->combo_variableSelect->setEnabled(enabled);
    if(enabled)
    {
        ui->combo_variableSelect->show();
        ui->label_variable->show();
        ui->button_describeVariable->show();
    }
    else
    {
        ui->combo_variableSelect->hide();
        ui->label_variable->hide();
        ui->button_describeVariable->hide();
    }
    return;
}

//-------------------------------------------//
//Bring up the browse for file dialog
//-------------------------------------------//
void mov_dialog_addtimeseries::on_browse_filebrowse_clicked()
{
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

        this->setItemsByFiletype();

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
    this->layer = ui->spin_layer->value();
    this->dFlowVariable = ui->combo_variableSelect->currentText();
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
        this->xadjust = this->xadjust / 3600.0;
    else if(ui->combo_timeSelect->currentText()=="minutes")
        this->xadjust = this->xadjust / 60.0;
    else if(ui->combo_timeSelect->currentText()=="days")
        this->xadjust = this->xadjust * 24.0;

    TempString = ui->text_yadjust->text();
    if(TempString==NULL)
        this->yadjust = 0.0;
    else
        this->yadjust = TempString.toDouble();

    if(this->InputFileName==NULL)
    {
        emit addTimeseriesError("Please select an input file.");
        return;
    }
    else if(this->InputSeriesName==NULL)
    {
        emit addTimeseriesError("Please input a series name.");
        return;
    }
    else if(this->InputColorString==NULL)
    {
        emit addTimeseriesError("Please select a valid color for this series.");
        return;
    }
    else if(this->InputStationFile==NULL && this->InputFileType==FILETYPE_ASCII_ADCIRC)
    {
        emit addTimeseriesError("You did not select a station file.");
        return;
    }
    else if(!this->epsgmap->contains(this->epsg))
    {
        emit addTimeseriesError("You did not enter a valid EPSG coordinate system.");
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
        QString description = this->epsgmap->value(ui->spin_epsg->value());
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


void mov_dialog_addtimeseries::on_combo_variableSelect_currentIndexChanged(const QString &arg1)
{
    this->dFlowVariable = arg1;
    return;
}
