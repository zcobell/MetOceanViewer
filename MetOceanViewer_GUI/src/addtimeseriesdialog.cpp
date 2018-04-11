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
#include "mainwindow.h"
#include "colors.h"
#include "filetypes.h"
#include "generic.h"
#include "netcdftimeseries.h"
#include "addtimeseriesdialog.h"
#include "ui_addtimeseriesdialog.h"

//-------------------------------------------//
// This brings up the dialog box used to add
// a file to the table of files
//-------------------------------------------//
AddTimeseriesDialog::AddTimeseriesDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::AddTimeseriesDialog) {
  ui->setupUi(this);
  ui->text_unitconvert->setValidator(new QDoubleValidator(this));
  ui->text_xadjust->setValidator(new QDoubleValidator(this));
  ui->text_yadjust->setValidator(new QDoubleValidator(this));
  this->PreviousDirectory = ((MainWindow *)parent)->PreviousDirectory;
  connect(this, SIGNAL(addTimeseriesError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));
  this->NumIMEDSFiles = 0;
  this->CurrentRowsInTable = 0;
  this->ColorUpdated = false;
  this->FileReadError = false;
  this->EditBox = false;
  this->UnitConversion = 1.0;
  this->xadjust = 0.0;
  this->yadjust = 0.0;
  this->InputFileType = 0;
  this->epsg = 4326;
  this->layer = 0;
  this->dflow = nullptr;
  this->proj = new proj4(this);
}
//-------------------------------------------//

void AddTimeseriesDialog::throwErrorMessageBox(QString errorString) {
  QMessageBox::critical(this, tr("ERROR"), errorString);
  return;
}

//-------------------------------------------//
// Destructor routine for the add imeds window
//-------------------------------------------//
AddTimeseriesDialog::~AddTimeseriesDialog() { delete ui; }
//-------------------------------------------//

//-------------------------------------------//
// Set the default elements with a series name,
// a blank filename, and a randomly generated color
//-------------------------------------------//
void AddTimeseriesDialog::set_default_dialog_box_elements(
    int NumRowsInTable) {
  QString ButtonStyle;
  this->InputFileColdStart.setTimeSpec(Qt::UTC);
  ui->text_seriesname->setText(tr("Series ") +
                               QString::number(NumRowsInTable + 1));
  ui->text_unitconvert->setText("1.0");
  ui->text_xadjust->setText("0.0");
  ui->text_yadjust->setText("0.0");
  ui->date_coldstart->setDateTime(QDateTime::currentDateTime());
  this->InputFileColdStart = ui->date_coldstart->dateTime();
  this->RandomButtonColor = Colors::GenerateRandomColor();
  this->InputColorString = Colors::getHexColor(this->RandomButtonColor);
  ButtonStyle = Colors::MakeColorString(this->RandomButtonColor);
  ui->button_seriesColor->setStyleSheet(ButtonStyle);
  ui->button_seriesColor->update();
  this->CurrentFileName = QString();
  this->epsg = 4326;
  this->dFlowVariable = QString();
  this->layer = 1;

  this->setColdstartSelectElements(false);
  this->setStationSelectElements(false);
  this->setVariableSelectElements(false);
  this->setVerticalLayerElements(false);

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// When editing an existing row, set the dialog
// box elements that we're bringing up for the user
//-------------------------------------------//
void AddTimeseriesDialog::set_dialog_box_elements(
    QString Filename, QString Filepath, QString SeriesName, double UnitConvert,
    double xmove, double ymove, QColor Color, QDateTime ColdStart, int FileType,
    QString StationPath, int epsg, QString varname, int layer) {
  QString ButtonStyle, StationFile;
  this->InputFileColdStart.setTimeSpec(Qt::UTC);
  Generic::splitPath(StationPath, StationFile, this->PreviousDirectory);
  ui->text_seriesname->setText(SeriesName);
  ui->text_filename->setText(Filename);
  ui->text_unitconvert->setText(QString::number(UnitConvert));
  ui->text_xadjust->setText(QString::number(xmove));
  ui->text_yadjust->setText(QString::number(ymove));
  ui->text_filetype->setText(Filetypes::integerFiletypeToString(FileType));
  ui->date_coldstart->setDateTime(ColdStart);
  ui->text_stationfile->setText(StationFile);
  this->InputFilePath = Filepath;
  this->CurrentFileName = Filepath;
  this->StationFilePath = StationPath;
  this->InputFileType = FileType;
  this->dFlowVariable = varname;
  this->epsg = epsg;
  this->layer = layer;
  ButtonStyle = Colors::MakeColorString(Color);
  this->RandomButtonColor = Color;
  ui->button_seriesColor->setStyleSheet(ButtonStyle);
  ui->button_seriesColor->update();

  this->setItemsByFiletype();

  return;
}
//-------------------------------------------//

void AddTimeseriesDialog::setItemsByFiletype() {
  if (this->InputFileType == MetOceanViewer::FileType::ASCII_IMEDS) {
    ui->text_filetype->setText("IMEDS");
    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    this->FileReadError = false;
  } else if (this->InputFileType == MetOceanViewer::FileType::NETCDF_ADCIRC) {
    ui->text_filetype->setText(QStringLiteral("ADCIRC netCDF"));
    ui->date_coldstart->setEnabled(true);
    this->setColdstartSelectElements(true);
    this->setStationSelectElements(true);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    this->FileReadError = false;
  } else if (this->InputFileType == MetOceanViewer::FileType::ASCII_ADCIRC) {
    ui->text_filetype->setText(QStringLiteral("ADCIRC ASCII"));
    this->setColdstartSelectElements(true);
    this->setStationSelectElements(true);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    this->FileReadError = false;
  } else if (this->InputFileType == MetOceanViewer::FileType::NETCDF_DFLOW) {
    QString variable = this->dFlowVariable;

    ui->text_filetype->setText(QStringLiteral("DFlow-FM"));
    this->FileReadError = false;

    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(true);

    this->dflow = new Dflow(this->InputFilePath, this);

    if (this->dflow->error->isError()) {
      emit addTimeseriesError(this->dflow->error->toString());
      this->FileReadError = true;
      return;
    }

    if (this->dflow->is3d()) {
      this->setVerticalLayerElements(true);
      ui->spin_layer->setMinimum(1);
      ui->spin_layer->setMaximum(dflow->getNumLayers());
      ui->spin_layer->setValue(this->layer);
      ui->label_layerinfo->setText(tr("Layer 1 = bottom\nLayer ") +
                                   QString::number(dflow->getNumLayers()) +
                                   tr(" = top"));

    } else
      this->setVerticalLayerElements(false);

    ui->combo_variableSelect->clear();
    QStringList dflowVariables = this->dflow->getVaribleList();
    ui->combo_variableSelect->addItems(dflowVariables);

    if (dflowVariables.contains(variable))
      ui->combo_variableSelect->setCurrentIndex(
          dflowVariables.indexOf(variable));
    else
      ui->combo_variableSelect->setCurrentIndex(0);

    this->dFlowVariable = variable;
  } else if (this->InputFileType == MetOceanViewer::FileType::NETCDF_GENERIC) {
    ui->text_filetype->setText("Generic netCDF");
    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    int epsg = NetcdfTimeseries::getEpsg(this->InputFilePath);
    if (epsg != 0)
      ui->spin_epsg->setValue(epsg);
    else
      ui->spin_epsg->setValue(4326);
    this->FileReadError = false;
  } else {
    this->FileReadError = true;
    emit addTimeseriesError(tr("No suitable filetype found."));
  }
  return;
}

void AddTimeseriesDialog::setVerticalLayerElements(bool enabled) {
  ui->spin_layer->setEnabled(enabled);
  if (enabled) {
    ui->spin_layer->show();
    ui->label_layer->show();
    ui->label_layerinfo->show();
  } else {
    ui->spin_layer->hide();
    ui->label_layer->hide();
    ui->label_layerinfo->hide();
  }
  return;
}

void AddTimeseriesDialog::setStationSelectElements(bool enabled) {
  ui->browse_stationfile->setEnabled(enabled);
  ui->text_stationfile->setEnabled(enabled);
  if (enabled) {
    ui->browse_stationfile->show();
    ui->text_stationfile->show();
    ui->label_stationfile->show();
  } else {
    ui->browse_stationfile->hide();
    ui->text_stationfile->hide();
    ui->label_stationfile->hide();
  }
  return;
}

void AddTimeseriesDialog::setColdstartSelectElements(bool enabled) {
  ui->date_coldstart->setEnabled(enabled);
  if (enabled) {
    ui->date_coldstart->show();
    ui->label_coldstart->show();
  } else {
    ui->date_coldstart->hide();
    ui->label_coldstart->hide();
  }
  return;
}

void AddTimeseriesDialog::setVariableSelectElements(bool enabled) {
  ui->combo_variableSelect->setEnabled(enabled);
  if (enabled) {
    ui->combo_variableSelect->show();
    ui->label_variable->show();
  } else {
    ui->combo_variableSelect->hide();
    ui->label_variable->hide();
  }
  return;
}

//-------------------------------------------//
// Bring up the browse for file dialog
//-------------------------------------------//
void AddTimeseriesDialog::on_browse_filebrowse_clicked() {
  QString Directory, filename, TempFile;

  if (this->EditBox)
    Generic::splitPath(this->InputFilePath, filename, Directory);
  else
    Directory = this->PreviousDirectory;

  QString TempPath = QFileDialog::getOpenFileName(
      this, tr("Select File"), Directory,
      tr("MetOceanViewer Compatible file (*.imeds *.61 *.62 *.71 *.72 *.nc) ;; "
         "IMEDS File (*.imeds *.IMEDS) ;; netCDF Output Files (*.nc) ;; "
         "DFlow-FM History Files (*_his.nc) ;; "
         "ADCIRC Output Files (*.61 *.62 *.71 *.72) ;; All Files (*.*)"));

  this->InputFilePath = TempPath;
  if (TempPath != NULL || (TempPath == NULL && this->CurrentFileName != NULL)) {
    if (TempPath == NULL) {
      TempPath = this->CurrentFileName;
      this->InputFilePath = this->CurrentFileName;
    } else
      this->CurrentFileName = TempPath;

    Generic::splitPath(TempPath, TempFile, this->PreviousDirectory);
    ui->text_filename->setText(TempFile);

    this->FileReadError = false;
    this->InputFileType =
        Filetypes::getIntegerFiletype(this->CurrentFileName);

    this->setItemsByFiletype();
  }
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Bring up a color palette and change the button color
// in the dialog when return comes
//-------------------------------------------//
void AddTimeseriesDialog::on_button_seriesColor_clicked() {
  QColor TempColor = QColorDialog::getColor(this->RandomButtonColor);
  QString ButtonStyle;

  this->ColorUpdated = false;

  if (TempColor.isValid()) {
    this->RandomButtonColor = TempColor;
    this->ColorUpdated = true;
    ButtonStyle = Colors::MakeColorString(this->RandomButtonColor);
    ui->button_seriesColor->setStyleSheet(ButtonStyle);
    ui->button_seriesColor->update();
  }

  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Browse dialog for a station file
//-------------------------------------------//
void AddTimeseriesDialog::on_browse_stationfile_clicked() {
  QString TempFile;
  QString TempPath = QFileDialog::getOpenFileName(
      this, tr("Select ADCIRC Station File"), this->PreviousDirectory,
      tr("Station Format Files (*.txt *.csv) ;; Text File (*.txt) ;; )"
         "Comma Separated File (*.csv) ;; All Files (*.*)"));
  if (TempPath != NULL) {
    this->StationFilePath = TempPath;
    Generic::splitPath(TempPath, TempFile, this->PreviousDirectory);
    ui->text_stationfile->setText(TempFile);
  }
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Redefine the local accept event with some validation
// of the dialog box
//-------------------------------------------//
void AddTimeseriesDialog::accept() {
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
  if (TempString == NULL)
    this->UnitConversion = 1.0;
  else
    this->UnitConversion = TempString.toDouble();

  TempString = ui->text_xadjust->text();
  if (TempString == NULL)
    this->xadjust = 0.0;
  else
    this->xadjust = TempString.toDouble();

  //...Convert to other time units
  if (ui->combo_timeSelect->currentText() == "seconds")
    this->xadjust = this->xadjust / 3600.0;
  else if (ui->combo_timeSelect->currentText() == "minutes")
    this->xadjust = this->xadjust / 60.0;
  else if (ui->combo_timeSelect->currentText() == "days")
    this->xadjust = this->xadjust * 24.0;

  TempString = ui->text_yadjust->text();
  if (TempString == NULL)
    this->yadjust = 0.0;
  else
    this->yadjust = TempString.toDouble();

  if (this->InputFileName == NULL) {
    emit addTimeseriesError(tr("Please select an input file."));
    return;
  } else if (this->InputSeriesName == NULL) {
    emit addTimeseriesError(tr("Please input a series name."));
    return;
  } else if (this->InputColorString == NULL) {
    emit addTimeseriesError(tr("Please select a valid color for this series."));
    return;
  } else if (this->InputStationFile == NULL &&
             this->InputFileType == MetOceanViewer::FileType::ASCII_ADCIRC) {
    emit addTimeseriesError(tr("You did not select a station file."));
    return;
  } else if (!this->proj->containsEPSG(this->epsg)) {
    emit addTimeseriesError(
        tr("You did not enter a valid EPSG coordinate system."));
  } else
    QDialog::accept();
}
//-------------------------------------------//

void AddTimeseriesDialog::on_button_presetColor1_clicked() {
  this->ColorUpdated = true;
  ui->button_seriesColor->setStyleSheet(ui->button_presetColor1->styleSheet());
  this->RandomButtonColor =
      Colors::styleSheetToColor(ui->button_seriesColor->styleSheet());
  ui->button_seriesColor->update();
}

void AddTimeseriesDialog::on_button_presetColor2_clicked() {
  this->ColorUpdated = true;
  ui->button_seriesColor->setStyleSheet(ui->button_presetColor2->styleSheet());
  this->RandomButtonColor =
      Colors::styleSheetToColor(ui->button_seriesColor->styleSheet());
  ui->button_seriesColor->update();
}

void AddTimeseriesDialog::on_button_presetColor3_clicked() {
  this->ColorUpdated = true;
  ui->button_seriesColor->setStyleSheet(ui->button_presetColor3->styleSheet());
  this->RandomButtonColor =
      Colors::styleSheetToColor(ui->button_seriesColor->styleSheet());
  ui->button_seriesColor->update();
}

void AddTimeseriesDialog::on_button_presetColor4_clicked() {
  this->ColorUpdated = true;
  ui->button_seriesColor->setStyleSheet(ui->button_presetColor4->styleSheet());
  this->RandomButtonColor =
      Colors::styleSheetToColor(ui->button_seriesColor->styleSheet());
  ui->button_seriesColor->update();
}

void AddTimeseriesDialog::on_button_describeepsg_clicked() {
  if (this->proj->containsEPSG(ui->spin_epsg->value())) {
    QString description =
        this->proj->coordinateSystemString(ui->spin_epsg->value());
    QMessageBox::information(this, tr("Coordinate System Description"),
                             description);
  } else
    QMessageBox::information(this, tr("Coordinate System Description"),
                             tr("ERROR: Invalid EPSG"));
}

void AddTimeseriesDialog::on_spin_epsg_valueChanged(int arg1) {
  if (this->proj->containsEPSG(arg1))
    ui->spin_epsg->setStyleSheet("background-color: rgb(255, 255, 255);");
  else
    ui->spin_epsg->setStyleSheet("background-color: rgb(255, 0, 0);");
  return;
}

void AddTimeseriesDialog::on_combo_variableSelect_currentIndexChanged(
    const QString &arg1) {
  this->dFlowVariable = arg1;
  if (this->dflow->variableIs3d(arg1)) {
    ui->spin_layer->show();
    ui->label_layer->show();
    ui->label_layerinfo->show();
  } else {
    ui->spin_layer->hide();
    ui->label_layer->hide();
    ui->label_layerinfo->hide();
  }
  return;
}
