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

#include "mainwindow.h"
#include "aboutdialog.h"
#include "colors.h"
#include "dflow.h"
#include "generic.h"
#include "keyhandler.h"
#include "mainwindow.h"
#include "noaa.h"
#include "session.h"
#include "ui_mainwindow.h"
#include "updatedialog.h"
#include "usgs.h"
#include "webenginepage.h"
#include "xtide.h"

MainWindow::MainWindow(bool processCommandLine, QString commandLineFile,
                       QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  this->processCommandLine = processCommandLine;
  this->commandLineFile = commandLineFile;
  this->setupMetOceanViewerUI();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::setupMetOceanViewerUI() {
  this->setupNoaaMap();
  this->setupUsgsMap();
  this->setupXTideMap();
  this->setupUserTimeseriesMap();
  this->setupTimeseriesTable();
  this->setupRandomColors();
  this->installKeyhandlers();

  this->localTimezoneOffset = Generic::getLocalTimzoneOffset();
  this->localTimeUTC = QDateTime::currentDateTimeUtc();

  this->previousDirectory = QDir::homePath();
#ifdef Q_OS_WIN
  this->previousDirectory = this->previousDirectory + "/Desktop";
#endif

  this->initializeSessionHandler();
  this->parseCommandLine();

  return;
}

void MainWindow::on_actionQuit_triggered() { this->close(); }

void MainWindow::closeEvent(QCloseEvent *event) {
  if (confirmClose())
    event->accept();
  else
    event->ignore();
}

bool MainWindow::confirmClose() {
  QMessageBox::StandardButton answer;
  answer = QMessageBox::question(this, tr("Exit"),
                                 tr("Do you want to exit MetOcean Viewer?"),
                                 QMessageBox::Yes | QMessageBox::No);
  return (answer == QMessageBox::Yes);
}

void MainWindow::on_actionAbout_triggered() {
  QPointer<AboutDialog> aboutWindow = new AboutDialog(this);
  aboutWindow->setModal(false);
  aboutWindow->exec();
  return;
}

void MainWindow::on_actionCheck_For_Updates_triggered() {
  QPointer<UpdateDialog> updateWindow = new UpdateDialog(this);
  updateWindow->setModal(false);
  updateWindow->runUpdater();
  updateWindow->exec();
  return;
}

void MainWindow::on_actionLoad_Session_triggered() {
  QString BaseFile;
  QString LoadFile = QFileDialog::getOpenFileName(
      this, tr("Open Session..."), this->previousDirectory,
      tr("MetOcean Viewer Sessions (*.mvs)"));

  if (LoadFile == NULL) return;

  Generic::splitPath(LoadFile, BaseFile, this->previousDirectory);
  int ierr = this->sessionState->open(LoadFile);

  if (ierr == 0) {
    ui->MainTabs->setCurrentIndex(1);
    ui->subtab_timeseries->setCurrentIndex(0);
  }

  return;
}

void MainWindow::on_actionSave_Session_triggered() {
  if (this->sessionState->getSessionFilename() != QString())
    this->sessionState->save();
  else
    on_actionSave_Session_As_triggered();
  return;
}

void MainWindow::on_actionSave_Session_As_triggered() {
  QString SaveFile = QFileDialog::getSaveFileName(
      this, tr("Save Session..."), this->previousDirectory,
      tr("MetOcean Viewer Sessions (*.mvs)"));
  if (SaveFile != NULL) {
    this->sessionState->setSessionFilename(SaveFile);
    this->sessionState->save();
  }
  return;
}

void MainWindow::handleEnterKey() {
  // Events for "ENTER" on the Live Data tabs
  if (ui->MainTabs->currentIndex() == 0) {
    if (ui->subtab_livedata->currentIndex() == 0) {
      on_Button_FetchData_clicked();
    } else if (ui->subtab_livedata->currentIndex() == 1) {
      on_button_usgs_fetch_clicked();
    } else if (ui->subtab_livedata->currentIndex() == 2) {
      on_button_xtide_compute_clicked();
    }
  }
  // Events for "ENTER" on the timeseries tabs
  else if (ui->MainTabs->currentIndex() == 1) {
    if (ui->subtab_timeseries->currentIndex() == 0) {
      on_button_processTimeseriesData_clicked();
    } else if (ui->subtab_timeseries->currentIndex() == 1) {
      on_button_plotTimeseriesStation_clicked();
    }
  } else if (ui->MainTabs->currentIndex() == 2) {
    if (ui->subtab_hwm->currentIndex() == 1) on_button_processHWM_clicked();
  }
  return;
}

void MainWindow::throwErrorMessageBox(QString errorString) {
  QMessageBox::critical(this, tr("ERROR"), errorString);
  return;
}

void MainWindow::setLoadSessionFile(bool toggle, QString sessionFile) {
  this->processCommandLine = toggle;
  this->commandLineFile = sessionFile;
  return;
}

void MainWindow::changeNoaaMarker(QString markerId) {
  this->noaaSelectedStation = markerId;
  return;
}

void MainWindow::changeUsgsMarker(QString markerId) {
  this->usgsSelectedStation = markerId;
  return;
}

void MainWindow::changeXtideMarker(QString markerId) {
  this->xtideSelectedStation = markerId;
  return;
}

void MainWindow::changeUserMarker(QString markerId) {
  this->userSelectedStations = markerId;
  return;
}

void MainWindow::changeNoaaMaptype() {
  ui->quick_noaaMap->rootContext()->setContextProperty(
      "mapType", ui->combo_noaa_maptype->currentIndex());
  return;
}

void MainWindow::changeUsgsMaptype() {
  ui->quick_usgsMap->rootContext()->setContextProperty(
      "mapType", ui->combo_usgs_maptype->currentIndex());
  return;
}

void MainWindow::changeXtideMaptype() {
  ui->quick_xtideMap->rootContext()->setContextProperty(
      "mapType", ui->combo_xtide_maptype->currentIndex());
  return;
}

void MainWindow::changeUserMaptype() {
  ui->quick_timeseriesMap->rootContext()->setContextProperty(
      "mapType", ui->combo_user_maptype->currentIndex());
  return;
}

void MainWindow::setHwmMarkerCategories() { return; }

void MainWindow::setupNoaaMap() {
  this->noaaStationModel = new StationModel(this);
  ui->quick_noaaMap->rootContext()->setContextProperty("stationModel",
                                                       this->noaaStationModel);
  ui->quick_noaaMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_noaaMap);
  Generic::setEsriMapTypes(ui->combo_noaa_maptype);
  ui->combo_noaa_maptype->setCurrentIndex(0);
  this->changeNoaaMaptype();
  ui->quick_noaaMap->setSource(QUrl("qrc:/qml/qml/MapViewer.qml"));
  Noaa::addStationsToModel(this->noaaStationModel);
  QObject *noaaItem = ui->quick_noaaMap->rootObject();
  QObject::connect(noaaItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeNoaaMarker(QString)));
  ui->Date_StartTime->setDateTime(QDateTime::currentDateTimeUtc().addDays(-1));
  ui->Date_EndTime->setDateTime(QDateTime::currentDateTimeUtc());
  ui->Date_StartTime->setMaximumDateTime(QDateTime::currentDateTimeUtc());
  ui->Date_EndTime->setMaximumDateTime(QDateTime::currentDateTimeUtc());
  this->noaaDisplayValues = false;
  ui->combo_noaaTimezoneLocation->setCurrentIndex(12);
  MainWindow::on_combo_noaaTimezoneLocation_currentIndexChanged(
      ui->combo_noaaTimezoneLocation->currentIndex());
  QMetaObject::invokeMethod(noaaItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));

  return;
}

void MainWindow::setupUsgsMap() {
  ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-7));
  ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime());
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  ui->Date_usgsEnd->setMaximumDateTime(QDateTime::currentDateTime());
  ui->Date_usgsStart->setMaximumDateTime(QDateTime::currentDateTime());
  this->usgsDisplayValues = false;
  ui->combo_usgsTimezoneLocation->setCurrentIndex(12);
  MainWindow::on_combo_usgsTimezoneLocation_currentIndexChanged(
      ui->combo_usgsTimezoneLocation->currentIndex());

  this->usgsStationModel = new StationModel(this);
  ui->quick_usgsMap->rootContext()->setContextProperty("stationModel",
                                                       this->usgsStationModel);
  ui->quick_usgsMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_usgsMap);
  Generic::setEsriMapTypes(ui->combo_usgs_maptype);
  ui->combo_usgs_maptype->setCurrentIndex(0);
  this->changeUsgsMaptype();
  ui->quick_usgsMap->setSource(QUrl("qrc:/qml/qml/MapViewer.qml"));
  Usgs::addStationsToModel(this->usgsStationModel);
  QObject *usgsItem = ui->quick_usgsMap->rootObject();
  QObject::connect(usgsItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeUsgsMarker(QString)));
  QMetaObject::invokeMethod(usgsItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));

  return;
}

void MainWindow::setupXTideMap() {
  ui->date_xtide_start->setDateTime(QDateTime::currentDateTime().addDays(-7));
  ui->date_xtide_end->setDateTime(QDateTime::currentDateTime());
  this->xtideDisplayValues = false;
  this->xtideStationModel = new StationModel(this);
  ui->quick_xtideMap->rootContext()->setContextProperty(
      "stationModel", this->xtideStationModel);
  ui->quick_xtideMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_xtideMap);
  Generic::setEsriMapTypes(ui->combo_xtide_maptype);
  ui->combo_xtide_maptype->setCurrentIndex(0);
  this->changeXtideMaptype();
  ui->quick_xtideMap->setSource(QUrl("qrc:/qml/qml/MapViewer.qml"));
  XTide::addStationsToModel(this->xtideStationModel);
  QObject *xtideItem = ui->quick_xtideMap->rootObject();
  QObject::connect(xtideItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeXtideMarker(QString)));
  QMetaObject::invokeMethod(xtideItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));
  return;
}

void MainWindow::setupMarkerClasses(QQuickWidget *widget) {
  widget->rootContext()->setContextProperty(QString("hwmClass0"),
                                            ui->spin_class0->value());
  widget->rootContext()->setContextProperty(QString("hwmClass1"),
                                            ui->spin_class1->value());
  widget->rootContext()->setContextProperty(QString("hwmClass2"),
                                            ui->spin_class2->value());
  widget->rootContext()->setContextProperty(QString("hwmClass3"),
                                            ui->spin_class3->value());
  widget->rootContext()->setContextProperty(QString("hwmClass4"),
                                            ui->spin_class4->value());
  widget->rootContext()->setContextProperty(QString("hwmClass5"),
                                            ui->spin_class5->value());
  widget->rootContext()->setContextProperty(QString("hwmClass6"),
                                            ui->spin_class6->value());
  return;
}

void MainWindow::setupUserTimeseriesMap() {
  this->userDataStationModel = new StationModel(this);
  ui->quick_timeseriesMap->rootContext()->setContextProperty(
      "stationModel", this->userDataStationModel);
  ui->quick_timeseriesMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::MultipleSelect);
  Generic::setEsriMapTypes(ui->combo_user_maptype);
  this->setupMarkerClasses(ui->quick_timeseriesMap);
  this->changeUserMaptype();
  ui->quick_timeseriesMap->setSource(QUrl("qrc:/qml/qml/MapViewer.qml"));
  QObject *userTimeseriesItem = ui->quick_timeseriesMap->rootObject();
  QObject::connect(userTimeseriesItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeUserMarker(QString)));
  ui->date_TimeseriesStartDate->setDateTime(
      ui->date_TimeseriesStartDate->minimumDateTime());
  ui->date_TimeseriesEndDate->setDateTime(
      ui->date_TimeseriesEndDate->maximumDateTime());

  QMetaObject::invokeMethod(userTimeseriesItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));

  this->timeseriesDisplayValues = false;
  this->timeseriesHideInfoWindows = true;
  return;
}

void MainWindow::setupHighWaterMarkMap() {
  ui->map_hwm->load(QUrl("qrc:/rsc/html/hwm_map.html"));

  // Set the colors that are being used on the display page for various
  // things that will be displayed
  this->DotColorHWM.setRgb(11, 84, 255);
  this->LineColorRegression.setRgb(7, 145, 0);
  this->LineColor121Line.setRgb(255, 0, 0);
  this->LineColorBounds.setRgb(0, 0, 0);
  this->hwmDisplayValues = false;

  // Set the button color for high water marks
  QString ButtonStyle = Colors::MakeColorString(this->DotColorHWM);
  ui->button_hwmcolor->setStyleSheet(ButtonStyle);
  ui->button_hwmcolor->update();

  // Set the button color for the 1:1 line
  ButtonStyle = Colors::MakeColorString(this->LineColor121Line);
  ui->button_121linecolor->setStyleSheet(ButtonStyle);
  ui->button_121linecolor->update();

  // Set the button color for the linear regression line
  ButtonStyle = Colors::MakeColorString(this->LineColorRegression);
  ui->button_reglinecolor->setStyleSheet(ButtonStyle);
  ui->button_reglinecolor->update();

  // Set the button color for StdDev bounding lines
  ButtonStyle = Colors::MakeColorString(this->LineColorBounds);
  ui->button_boundlinecolor->setStyleSheet(ButtonStyle);
  ui->button_boundlinecolor->update();
}

void MainWindow::setupRandomColors() {
  //...Build a table of random colors
  this->randomColors.resize(16);
  this->randomColors[0] = QColor(Qt::GlobalColor::green);
  this->randomColors[1] = QColor(Qt::GlobalColor::red);
  this->randomColors[2] = QColor(Qt::GlobalColor::blue);
  this->randomColors[3] = QColor(Qt::GlobalColor::yellow);
  this->randomColors[4] = QColor(Qt::GlobalColor::magenta);
  this->randomColors[5] = QColor(Qt::GlobalColor::cyan);
  this->randomColors[6] = QColor(Qt::GlobalColor::black);
  this->randomColors[7] = QColor(Qt::GlobalColor::darkRed);
  this->randomColors[8] = QColor(Qt::GlobalColor::darkGreen);
  this->randomColors[9] = QColor(Qt::GlobalColor::darkBlue);
  this->randomColors[10] = QColor(Qt::GlobalColor::darkCyan);
  this->randomColors[11] = QColor(Qt::GlobalColor::darkMagenta);
  this->randomColors[12] = QColor(Qt::GlobalColor::darkYellow);
  this->randomColors[13] = QColor(Qt::GlobalColor::darkGray);
  this->randomColors[14] = QColor(Qt::GlobalColor::lightGray);
  this->randomColors[15] = QColor(Qt::GlobalColor::gray);
  return;
}

void MainWindow::checkForUpdate() {
  //...Check for updates and alert the user if there is a new version
  QPointer<UpdateDialog> update = new UpdateDialog(this);
  bool doUpdate = update->checkForUpdate();
  if (doUpdate) {
    update->runUpdater();
    update->exec();
  }
  return;
}

void MainWindow::installKeyhandlers() {
  Keyhandler *key = new Keyhandler(this);
  this->centralWidget()->installEventFilter(key);
  connect(key, SIGNAL(enterKeyPressed()), this, SLOT(handleEnterKey()));

//...This accounts for a bug in Qt v5.9.1
//   If the user cursor is not in the map, the
//   enter key "plot" signal is not sent. In the
//   case of Qt5.9.1, we make this connection manually
//   but otherwise there is no need.
#if (QT_VERSION == QT_VERSION_CHECK(5, 9, 1))
  connect(ui->noaa_map, SIGNAL(enterKeyPressed()), this,
          SLOT(handleEnterKey()));
  connect(ui->usgs_map, SIGNAL(enterKeyPressed()), this,
          SLOT(handleEnterKey()));
  connect(ui->timeseries_map, SIGNAL(enterKeyPressed()), this,
          SLOT(handleEnterKey()));
  connect(ui->xtide_map, SIGNAL(enterKeyPressed()), this,
          SLOT(handleEnterKey()));
#endif
  return;
}

void MainWindow::parseCommandLine() {
  if (this->processCommandLine) {
    int ierr = this->sessionState->open(this->commandLineFile);
    if (ierr == 0) {
      ui->MainTabs->setCurrentIndex(1);
      ui->subtab_timeseries->setCurrentIndex(0);
    }
  }
  return;
}

void MainWindow::initializeSessionHandler() {
  this->sessionState =
      new Session(ui->table_TimeseriesData, ui->text_TimeseriesPlotTitle,
                  ui->text_TimeseriesXaxisLabel, ui->text_TimeseriesYaxisLabel,
                  ui->date_TimeseriesStartDate, ui->date_TimeseriesEndDate,
                  ui->spin_TimeseriesYmin, ui->spin_TimeseriesYmax,
                  ui->check_TimeseriesAllData, ui->check_TimeseriesYauto,
                  this->previousDirectory, this);

  connect(this->sessionState, SIGNAL(sessionError(QString)), this,
          SLOT(throwErrorMessageBox(QString)));
  return;
}

void MainWindow::on_combo_xtide_maptype_currentIndexChanged(int index) {
  Q_UNUSED(index);
  this->changeXtideMaptype();
}

void MainWindow::on_combo_user_maptype_currentIndexChanged(int index) {
  Q_UNUSED(index);
  this->changeUserMaptype();
}
