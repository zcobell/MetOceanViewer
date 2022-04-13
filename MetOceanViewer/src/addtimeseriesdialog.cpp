/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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

#include "addtimeseriesdialog.h"
#include <QMessageBox>
#include <memory>
#include "colors.h"
#include "filetypes.h"
#include "generic.h"
#include "mainwindow.h"
#include "netcdftimeseries.h"
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
  this->m_previousDirectory =
      static_cast<MainWindow *>(parent)->previousDirectory;
  connect(this, SIGNAL(addTimeseriesError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));
  this->m_fileReadError = false;
  this->m_editBox = false;
  this->m_unitConversion = 1.0;
  this->m_xadjust = 0.0;
  this->m_yadjust = 0.0;
  this->m_inputFileType = 0;
  this->m_epsg = 4326;
  this->m_layer = 0;
  this->dflow = nullptr;
  this->m_lineStyle = Qt::SolidLine;
  this->proj = std::unique_ptr<Ezproj>(new Ezproj());

  this->m_unitMenu = this->buildUnitConversionMenu();
  ui->button_unitConversions->setMenu(this->m_unitMenu);
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
void AddTimeseriesDialog::set_default_dialog_box_elements(int NumRowsInTable) {
  this->m_inputFileColdstart.setTimeSpec(Qt::UTC);
  ui->text_seriesname->setText(tr("Series ") +
                               QString::number(NumRowsInTable + 1));
  ui->text_unitconvert->setText("1.0");
  ui->text_xadjust->setText("0.0");
  ui->text_yadjust->setText("0.0");
  ui->date_coldstart->setDateTime(QDateTime::currentDateTime());
  this->m_inputFileColdstart = ui->date_coldstart->dateTime();
  this->m_randomButtonColor = Colors::generateRandomColor();
  this->m_inputColorString = Colors::getHexColor(this->m_randomButtonColor);
  Colors::changeButtonColor(ui->button_seriesColor, this->m_randomButtonColor);
  this->m_currentFileName = QString();
  this->m_epsg = 4326;
  this->m_dflowVariable = QString();
  this->m_layer = 1;
  this->m_lineStyle = Qt::SolidLine;

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
    QString StationPath, int epsg, QString varname, int layer, int lineStyle) {
  QString StationFile;
  this->m_inputFileColdstart.setTimeSpec(Qt::UTC);
  Generic::splitPath(StationPath, StationFile, this->m_previousDirectory);
  ui->text_seriesname->setText(SeriesName);
  ui->text_filename->setText(Filename);
  ui->text_unitconvert->setText(QString::number(UnitConvert));
  ui->text_xadjust->setText(QString::number(xmove));
  ui->text_yadjust->setText(QString::number(ymove));
  ui->text_filetype->setText(Filetypes::integerFiletypeToString(FileType));
  ui->date_coldstart->setDateTime(ColdStart);
  ui->text_stationfile->setText(StationFile);
  ui->spin_epsg->setValue(epsg);
  ui->combo_linestyle->setCurrentIndex(lineStyle - 1);
  this->m_inputFilePath = Filepath;
  this->m_currentFileName = Filepath;
  this->m_stationFilePath = StationPath;
  this->m_inputFileType = FileType;
  this->m_dflowVariable = varname;
  this->m_epsg = epsg;
  this->m_layer = layer;
  this->m_randomButtonColor = Color;
  this->m_lineStyle = lineStyle;
  Colors::changeButtonColor(ui->button_seriesColor, this->m_randomButtonColor);
  this->setItemsByFiletype();

  return;
}
//-------------------------------------------//

void AddTimeseriesDialog::setItemsByFiletype() {
  if (this->m_inputFileType == MetOceanViewer::FileType::ASCII_IMEDS) {
    ui->text_filetype->setText("IMEDS");
    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    this->m_fileReadError = false;
  } else if (this->m_inputFileType == MetOceanViewer::FileType::NETCDF_ADCIRC) {
    ui->text_filetype->setText(QStringLiteral("ADCIRC netCDF"));
    ui->date_coldstart->setEnabled(true);
    this->setColdstartSelectElements(true);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    this->m_fileReadError = false;
  } else if (this->m_inputFileType == MetOceanViewer::FileType::ASCII_ADCIRC) {
    ui->text_filetype->setText(QStringLiteral("ADCIRC ASCII"));
    this->setColdstartSelectElements(true);
    this->setStationSelectElements(true);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    this->m_fileReadError = false;
  } else if (this->m_inputFileType == MetOceanViewer::FileType::NETCDF_DFLOW) {
    QString variable = this->m_dflowVariable;

    ui->text_filetype->setText(QStringLiteral("DFlow-FM"));
    this->m_fileReadError = false;

    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(true);

    this->dflow = new Dflow(this->m_inputFilePath, this);

    if (this->dflow->error->isError()) {
      emit addTimeseriesError(this->dflow->error->toString());
      this->m_fileReadError = true;
      return;
    }

    if (this->dflow->is3d()) {
      this->setVerticalLayerElements(true);
      ui->spin_layer->setMinimum(1);
      ui->spin_layer->setMaximum(dflow->getNumLayers());
      ui->spin_layer->setValue(this->m_layer);
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

    this->m_dflowVariable = variable;
  } else if (this->m_inputFileType ==
             MetOceanViewer::FileType::NETCDF_GENERIC) {
    ui->text_filetype->setText("Generic netCDF");
    this->setColdstartSelectElements(false);
    this->setStationSelectElements(false);
    this->setVariableSelectElements(false);
    this->setVerticalLayerElements(false);
    int epsg = NetcdfTimeseries::getEpsg(this->m_inputFilePath);
    if (epsg != 0)
      ui->spin_epsg->setValue(epsg);
    else
      ui->spin_epsg->setValue(4326);
    this->m_fileReadError = false;
  } else {
    this->m_fileReadError = true;
    emit addTimeseriesError(tr("No suitable filetype found."));
  }
  return;
}

QMenu *AddTimeseriesDialog::buildUnitConversionMenu() {
  QMenu *menu = new QMenu(this);
  QMenu *length = new QMenu(menu);
  QMenu *speed = new QMenu(menu);
  QMenu *pressure = new QMenu(menu);

  length->setTitle("Length");
  speed->setTitle("Speed");
  pressure->setTitle("Pressure");

  QAction *meters2feet = length->addAction("meters --> feet");
  QAction *feet2meters = length->addAction("feet --> meters");
  QAction *meters2inches = length->addAction("meters --> inches");
  QAction *feet2inches = length->addAction("feet --> inches");
  QAction *miles2meters = length->addAction("miles --> meters");
  QAction *miles2feet = length->addAction("miles --> feet");
  QAction *kilo2miles = length->addAction("kilometers --> miles");
  QAction *miles2kilo = length->addAction("miles --> kilometers");

  connect(meters2feet, &QAction::triggered,
          [this] { this->setUnitValue(3.28084); });
  connect(feet2meters, &QAction::triggered,
          [this] { this->setUnitValue(0.3048); });
  connect(meters2inches, &QAction::triggered,
          [this] { this->setUnitValue(39.3701); });
  connect(feet2inches, &QAction::triggered, [this] { this->setUnitValue(12.0); });
  connect(miles2meters, &QAction::triggered,
          [this] { this->setUnitValue(1609.34); });
  connect(miles2feet, &QAction::triggered, [this] { this->setUnitValue(5280.0); });
  connect(kilo2miles, &QAction::triggered,
          [this] { this->setUnitValue(0.621371); });
  connect(miles2kilo, &QAction::triggered,
          [this] { this->setUnitValue(1.60934); });

  QAction *mpsmph = speed->addAction("m/s --> mph");
  QAction *mphmps = speed->addAction("mph --> m/s");
  QAction *mphfps = speed->addAction("mph --> fps");
  QAction *mpskt = speed->addAction("m/s --> knots");
  QAction *ktmps = speed->addAction("knots --> m/s");
  QAction *mpsfps = speed->addAction("m/s --> f/s");
  QAction *fpsmps = speed->addAction("f/s --> m/s");
  QAction *fpskt = speed->addAction("f/s --> knots");
  QAction *ktfps = speed->addAction("knots --> f/s");

  connect(mpsmph, &QAction::triggered, [this] { this->setUnitValue(2.23694); });
  connect(mphmps, &QAction::triggered, [this] { this->setUnitValue(0.44704); });
  connect(mphfps, &QAction::triggered, [this] { this->setUnitValue(1.46667); });
  connect(mpskt, &QAction::triggered,  [this] { this->setUnitValue(1.94384); });
  connect(ktmps, &QAction::triggered,  [this] { this->setUnitValue(0.514444); });
  connect(mpsfps, &QAction::triggered, [this] { this->setUnitValue(3.28084); });
  connect(fpsmps, &QAction::triggered, [this] { this->setUnitValue(0.3048); });
  connect(fpskt, &QAction::triggered,  [this] { this->setUnitValue(0.592484); });
  connect(ktfps, &QAction::triggered,  [this] { this->setUnitValue(1.68781); });

  QAction *mh202mb = pressure->addAction("mH20 --> mb");
  QAction *mb2mh20 = pressure->addAction("mb --> mH20");
  QAction *mH202pa = pressure->addAction("mH20 --> pa");
  QAction *pa2mH20 = pressure->addAction("pa --> mH20");
  QAction *mb2pa = pressure->addAction("mb --> pa");
  QAction *pa2mb = pressure->addAction("pa --> mb");

  connect(mh202mb, &QAction::triggered, [this] { this->setUnitValue(98.07); });
  connect(mb2mh20, &QAction::triggered, [this] { this->setUnitValue(0.010197); });
  connect(mH202pa, &QAction::triggered, [this] { this->setUnitValue(9806.38); });
  connect(pa2mH20, &QAction::triggered,
          [this] { this->setUnitValue(0.00010197442889221); });
  connect(mb2pa, &QAction::triggered, [this] { this->setUnitValue(100.0); });
  connect(pa2mb, &QAction::triggered, [this] { this->setUnitValue(0.01); });

  menu->addMenu(length);
  menu->addMenu(speed);
  menu->addMenu(pressure);

  return menu;
}

void AddTimeseriesDialog::setUnitValue(double d) {
  ui->text_unitconvert->setText(QString::number(d));
}

int AddTimeseriesDialog::lineStyle() const { return this->m_lineStyle; }

void AddTimeseriesDialog::setLineStyle(int lineStyle) {
  this->m_lineStyle = lineStyle;
}

int AddTimeseriesDialog::layer() const { return m_layer; }

void AddTimeseriesDialog::setLayer(int layer) { m_layer = layer; }

int AddTimeseriesDialog::epsg() const { return m_epsg; }

void AddTimeseriesDialog::setEpsg(int epsg) { m_epsg = epsg; }

int AddTimeseriesDialog::inputFileType() const { return m_inputFileType; }

void AddTimeseriesDialog::setInputFileType(int inputFileType) {
  m_inputFileType = inputFileType;
}

QDateTime AddTimeseriesDialog::inputFileColdstart() const {
  return m_inputFileColdstart;
}

void AddTimeseriesDialog::setInputFileColdstart(
    const QDateTime &inputFileColdstart) {
  m_inputFileColdstart = inputFileColdstart;
}

QString AddTimeseriesDialog::currentFileName() const {
  return m_currentFileName;
}

void AddTimeseriesDialog::setCurrentFileName(const QString &currentFileName) {
  m_currentFileName = currentFileName;
}

QString AddTimeseriesDialog::inputStationFile() const {
  return m_inputStationFile;
}

void AddTimeseriesDialog::setInputStationFile(const QString &inputStationFile) {
  m_inputStationFile = inputStationFile;
}

QString AddTimeseriesDialog::dflowVariable() const { return m_dflowVariable; }

void AddTimeseriesDialog::setDflowVariable(const QString &dflowVariable) {
  m_dflowVariable = dflowVariable;
}

QString AddTimeseriesDialog::stationFilePath() const {
  return m_stationFilePath;
}

void AddTimeseriesDialog::setStationFilePath(const QString &stationFilePath) {
  m_stationFilePath = stationFilePath;
}

QString AddTimeseriesDialog::inputFilePath() const { return m_inputFilePath; }

void AddTimeseriesDialog::setInputFilePath(const QString &inputFilePath) {
  m_inputFilePath = inputFilePath;
}

QString AddTimeseriesDialog::inputSeriesName() const {
  return m_inputSeriesName;
}

void AddTimeseriesDialog::setInputSeriesName(const QString &inputSeriesName) {
  m_inputSeriesName = inputSeriesName;
}

QString AddTimeseriesDialog::inputColorString() const {
  return m_inputColorString;
}

void AddTimeseriesDialog::setInputColorString(const QString &inputColorString) {
  m_inputColorString = inputColorString;
}

QString AddTimeseriesDialog::inputFileName() const { return m_inputFileName; }

void AddTimeseriesDialog::setInputFileName(const QString &inputFileName) {
  m_inputFileName = inputFileName;
}

QColor AddTimeseriesDialog::randomButtonColor() const {
  return m_randomButtonColor;
}

void AddTimeseriesDialog::setRandomButtonColor(
    const QColor &randomButtonColor) {
  m_randomButtonColor = randomButtonColor;
}

double AddTimeseriesDialog::yadjust() const { return m_yadjust; }

void AddTimeseriesDialog::setYadjust(double yadjust) { m_yadjust = yadjust; }

double AddTimeseriesDialog::xadjust() const { return m_xadjust; }

void AddTimeseriesDialog::setXadjust(double xadjust) { m_xadjust = xadjust; }

double AddTimeseriesDialog::unitConversion() const { return m_unitConversion; }

void AddTimeseriesDialog::setUnitConversion(double unitConversion) {
  m_unitConversion = unitConversion;
}

bool AddTimeseriesDialog::editBox() const { return m_editBox; }

void AddTimeseriesDialog::setEditBox(bool editBox) { m_editBox = editBox; }

bool AddTimeseriesDialog::fileReadError() const { return m_fileReadError; }

void AddTimeseriesDialog::setFileReadError(bool fileReadError) {
  m_fileReadError = fileReadError;
}

QString AddTimeseriesDialog::previousDirectory() const {
  return m_previousDirectory;
}

void AddTimeseriesDialog::setPreviousDirectory(
    const QString &previousDirectory) {
  m_previousDirectory = previousDirectory;
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

  if (this->m_editBox)
    Generic::splitPath(this->m_inputFilePath, filename, Directory);
  else
    Directory = this->m_previousDirectory;

  QString TempPath = QFileDialog::getOpenFileName(
      this, tr("Select File"), Directory,
      tr("MetOceanViewer Compatible file (*.imeds *.61 *.62 *.71 *.72 *.nc) ;; "
         "IMEDS File (*.imeds *.IMEDS) ;; netCDF Output Files (*.nc) ;; "
         "DFlow-FM History Files (*_his.nc) ;; "
         "ADCIRC Output Files (*.61 *.62 *.71 *.72) ;; All Files (*.*)"));

  this->m_inputFilePath = TempPath;

  if (TempPath != QString() || this->m_currentFileName != QString()) {
    if (TempPath == QString()) {
      TempPath = this->m_currentFileName;
      this->m_inputFilePath = this->m_currentFileName;
    } else {
      this->m_currentFileName = TempPath;
    }

    Generic::splitPath(TempPath, TempFile, this->m_previousDirectory);
    ui->text_filename->setText(TempFile);

    this->m_fileReadError = false;
    this->m_inputFileType =
        Filetypes::getIntegerFiletype(this->m_currentFileName);

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
  Colors::selectButtonColor(ui->button_seriesColor);
  this->m_randomButtonColor =
      Colors::styleSheetToColor(ui->button_seriesColor->styleSheet());
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Browse dialog for a station file
//-------------------------------------------//
void AddTimeseriesDialog::on_browse_stationfile_clicked() {
  QString TempFile;
  QString TempPath = QFileDialog::getOpenFileName(
      this, tr("Select ADCIRC Station File"), this->m_previousDirectory,
      tr("Station Format Files (*.txt *.csv) ;; Text File (*.txt) ;; )"
         "Comma Separated File (*.csv) ;; All Files (*.*)"));
  if (TempPath != QString()) {
    this->m_stationFilePath = TempPath;
    Generic::splitPath(TempPath, TempFile, this->m_previousDirectory);
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

  this->m_inputFileName = ui->text_filename->text();
  this->m_inputColorString = this->m_randomButtonColor.name();
  this->m_inputSeriesName = ui->text_seriesname->text();
  this->m_inputFileColdstart = ui->date_coldstart->dateTime();
  this->m_epsg = ui->spin_epsg->value();
  this->m_layer = ui->spin_layer->value();
  this->m_dflowVariable = ui->combo_variableSelect->currentText();
  this->m_lineStyle = ui->combo_linestyle->currentIndex() + 1;
  TempString = ui->text_unitconvert->text();
  this->m_inputStationFile = ui->text_stationfile->text();
  if (TempString == QString())
    this->m_unitConversion = 1.0;
  else
    this->m_unitConversion = TempString.toDouble();

  TempString = ui->text_xadjust->text();
  if (TempString == QString())
    this->m_xadjust = 0.0;
  else
    this->m_xadjust = TempString.toDouble();

  //...Convert to other time units
  if (ui->combo_timeSelect->currentText() == "seconds")
    this->m_xadjust = this->m_xadjust / 3600.0;
  else if (ui->combo_timeSelect->currentText() == "minutes")
    this->m_xadjust = this->m_xadjust / 60.0;
  else if (ui->combo_timeSelect->currentText() == "days")
    this->m_xadjust = this->m_xadjust * 24.0;

  TempString = ui->text_yadjust->text();
  if (TempString == QString())
    this->m_yadjust = 0.0;
  else
    this->m_yadjust = TempString.toDouble();

  if (this->m_inputFileName == QString()) {
    emit addTimeseriesError(tr("Please select an input file."));
    return;
  } else if (this->m_inputSeriesName == QString()) {
    emit addTimeseriesError(tr("Please input a series name."));
    return;
  } else if (this->m_inputColorString == QString()) {
    emit addTimeseriesError(tr("Please select a valid color for this series."));
    return;
  } else if (this->m_inputStationFile == QString() &&
             this->m_inputFileType == MetOceanViewer::FileType::ASCII_ADCIRC) {
    emit addTimeseriesError(tr("You did not select a station file."));
    return;
  } else if (!this->proj->containsEpsg(this->m_epsg)) {
    emit addTimeseriesError(
        tr("You did not enter a valid EPSG coordinate system."));
  } else
    QDialog::accept();
}
//-------------------------------------------//

void AddTimeseriesDialog::on_button_presetColor1_clicked() {
  Colors::changeButtonColor(
      ui->button_seriesColor,
      ui->button_presetColor1->palette().color(QPalette::Button));
  this->m_randomButtonColor =
      ui->button_seriesColor->palette().color(QPalette::Button);
}

void AddTimeseriesDialog::on_button_presetColor2_clicked() {
  Colors::changeButtonColor(
      ui->button_seriesColor,
      ui->button_presetColor2->palette().color(QPalette::Button));
  this->m_randomButtonColor =
      ui->button_seriesColor->palette().color(QPalette::Button);
}

void AddTimeseriesDialog::on_button_presetColor3_clicked() {
  Colors::changeButtonColor(
      ui->button_seriesColor,
      ui->button_presetColor3->palette().color(QPalette::Button));
  this->m_randomButtonColor =
      ui->button_seriesColor->palette().color(QPalette::Button);
}

void AddTimeseriesDialog::on_button_presetColor4_clicked() {
  Colors::changeButtonColor(
      ui->button_seriesColor,
      ui->button_presetColor4->palette().color(QPalette::Button));
  this->m_randomButtonColor =
      ui->button_seriesColor->palette().color(QPalette::Button);
}

void AddTimeseriesDialog::on_button_describeepsg_clicked() {
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Coordinate System Description");
  msgBox.setTextFormat(Qt::RichText);
  QString description;

  if (this->proj->containsEpsg(ui->spin_epsg->value())) {
    int proj4code = ui->spin_epsg->value();
    QString projInitString =
        QString::fromStdString(this->proj->projInitializationString(proj4code));
    QString csDescription =
        QString::fromStdString(this->proj->description(proj4code));
    description = QStringLiteral(
                      "<b>Coordinate System Reference:</b> <a "
                      "href=\"http://spatialreference.org/ref/epsg/") +
                  QString::number(proj4code) +
                  QStringLiteral("/\">SpatialReference.org</a>") +
                  QStringLiteral("<br><b>Coordinate System Description:</b> ") +
                  csDescription +
                  QStringLiteral("<br><b>Proj4 Initialization String:</b> ") +
                  projInitString;
  } else {
    description = "<b>Error:</b> Invalid EPSG";
  }
  msgBox.setText(description);
  msgBox.setModal(true);
  msgBox.exec();
  msgBox.deleteLater();
  return;
}

void AddTimeseriesDialog::on_spin_epsg_valueChanged(int arg1) {
  if (this->proj->containsEpsg(arg1))
    ui->spin_epsg->setStyleSheet("background-color: rgb(255, 255, 255);");
  else
    ui->spin_epsg->setStyleSheet("background-color: rgb(255, 0, 0);");
  return;
}

void AddTimeseriesDialog::on_combo_variableSelect_currentIndexChanged(
    const QString &arg1) {
  this->m_dflowVariable = arg1;
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
