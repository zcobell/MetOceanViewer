/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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
#include <QGeoRectangle>
#include <QGeoShape>
#include <QQmlContext>
#include <algorithm>
#include "aboutdialog.h"
#include "colors.h"
#include "dflow.h"
#include "generic.h"
#include "keyhandler.h"
#include "mainwindow.h"
#include "mapfunctions.h"
#include "noaa.h"
#include "session.h"
#include "ui_mainwindow.h"
#include "updatedialog.h"
#include "usgs.h"
#include "xtide.h"

MainWindow::MainWindow(bool processCommandLine, QString commandLineFile,
                       QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  this->processCommandLine = processCommandLine;
  this->commandLineFile = commandLineFile;
  this->initialized = false;
  this->setupMetOceanViewerUI();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::resetMapSource(MapFunctions::MapSource source) {
  this->mapFunctions->setDefaultMapIndex(0);
  this->mapFunctions->setMapSource(source);
  this->setupNoaaMap();
  this->setupUsgsMap();
  this->setupXTideMap();
  this->setupUserTimeseriesMap();
  this->setupHighWaterMarkMap();
  return;
}

void MainWindow::setupMetOceanViewerUI() {
  this->mapFunctions = new MapFunctions(this);
  this->mapFunctions->setMapSource(MapFunctions::MapSource::ESRI);
  this->mapFunctions->getMapboxKeyFromDisk();
  this->mapFunctions->getDefaultMapTypeFromDisk();

  if (this->mapFunctions->mapSource() == MapFunctions::MapSource::MapBox &&
      this->mapFunctions->mapboxApiKey() == QString()) {
    this->mapFunctions->setDefaultMapIndex(0);
    this->mapFunctions->setMapSource(MapFunctions::MapSource::ESRI);
  }

  if (this->mapFunctions->mapSource() == MapFunctions::MapSource::ESRI) {
    ui->actionESRI->setChecked(true);
    ui->actionMapBox->setChecked(false);
  } else {
    ui->actionESRI->setChecked(false);
    ui->actionMapBox->setChecked(true);
  }

  this->noaaStationModel = nullptr;
  this->usgsStationModel = nullptr;
  this->xtideStationModel = nullptr;
  this->userDataStationModel = nullptr;
  this->hwmMarkerModel = nullptr;

  this->setupNoaaMap();
  this->setupUsgsMap();
  this->setupXTideMap();
  this->setupUserTimeseriesMap();
  this->setupHighWaterMarkMap();
  this->setupTimeseriesTable();
  this->setupRandomColors();
  this->installKeyhandlers();

  this->previousDirectory = QDir::homePath();
#ifdef Q_OS_WIN
  this->previousDirectory = this->previousDirectory + "/Desktop";
#endif

  this->initializeSessionHandler();
  this->parseCommandLine();
  this->initialized = true;

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

void MainWindow::changeHwmMaptype() {
  ui->quick_hwmMap->rootContext()->setContextProperty(
      "mapType", ui->combo_hwmMaptype->currentIndex());
  return;
}

void MainWindow::setHwmMarkerCategories() { return; }

void MainWindow::setupNoaaMap() {
  if (this->noaaStationModel == nullptr) {
    this->noaaStationModel = new StationModel(this);
  }
  ui->quick_noaaMap->rootContext()->setContextProperty("stationModel",
                                                       this->noaaStationModel);
  ui->quick_noaaMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_noaaMap);
  this->mapFunctions->setMapTypes(ui->combo_noaa_maptype);
  ui->combo_noaa_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->changeNoaaMaptype();
  this->mapFunctions->setMapQmlFile(ui->quick_noaaMap);
  this->noaaMarkerLocations =
      this->mapFunctions->readMarkers(MapFunctions::NOAA);
  QObject *noaaItem = ui->quick_noaaMap->rootObject();
  QObject::connect(noaaItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeNoaaMarker(QString)));
  ui->Date_StartTime->setDateTime(QDateTime::currentDateTimeUtc().addDays(-1));
  ui->Date_EndTime->setDateTime(QDateTime::currentDateTimeUtc());

  ui->combo_noaaTimezoneLocation->setCurrentIndex(12);
  MainWindow::on_combo_noaaTimezoneLocation_currentIndexChanged(
      ui->combo_noaaTimezoneLocation->currentIndex());
  QMetaObject::invokeMethod(noaaItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));
  this->on_button_refreshNoaaStations_clicked();

  return;
}

void MainWindow::setupUsgsMap() {
  if (this->usgsStationModel == nullptr) {
    this->usgsStationModel = new StationModel(this);
  }
  ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-7));
  ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime());
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1)));
  ui->Date_usgsEnd->setMaximumDateTime(QDateTime::currentDateTime());
  ui->Date_usgsStart->setMaximumDateTime(QDateTime::currentDateTime());
  ui->combo_usgsTimezoneLocation->setCurrentIndex(12);
  MainWindow::on_combo_usgsTimezoneLocation_currentIndexChanged(
      ui->combo_usgsTimezoneLocation->currentIndex());

  ui->quick_usgsMap->rootContext()->setContextProperty("stationModel",
                                                       this->usgsStationModel);
  ui->quick_usgsMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_usgsMap);
  this->usgsMarkerLocations =
      this->mapFunctions->readMarkers(MapFunctions::USGS);
  this->mapFunctions->setMapTypes(ui->combo_usgs_maptype);
  ui->combo_usgs_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->changeUsgsMaptype();
  this->mapFunctions->setMapQmlFile(ui->quick_usgsMap);
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

  if (this->xtideSelectedStation == nullptr) {
    this->xtideStationModel = new StationModel(this);
  }

  ui->quick_xtideMap->rootContext()->setContextProperty(
      "stationModel", this->xtideStationModel);
  ui->quick_xtideMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_xtideMap);
  this->xtideMarkerLocations =
      this->mapFunctions->readMarkers(MapFunctions::XTIDE);
  this->mapFunctions->setMapTypes(ui->combo_xtide_maptype);
  ui->combo_xtide_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->changeXtideMaptype();
  this->mapFunctions->setMapQmlFile(ui->quick_xtideMap);
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
  if (this->userDataStationModel == nullptr) {
    this->userDataStationModel = new StationModel(this);
  }

  ui->quick_timeseriesMap->rootContext()->setContextProperty(
      "stationModel", this->userDataStationModel);
  ui->quick_timeseriesMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::MultipleSelect);
  this->mapFunctions->setMapTypes(ui->combo_user_maptype);
  this->setupMarkerClasses(ui->quick_timeseriesMap);
  ui->combo_user_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->changeUserMaptype();
  this->mapFunctions->setMapQmlFile(ui->quick_timeseriesMap);
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
  return;
}

void MainWindow::setupHighWaterMarkMap() {
  if (this->hwmMarkerModel != nullptr) {
    this->hwmMarkerModel = new StationModel(this);
  }

  ui->quick_hwmMap->rootContext()->setContextProperty("stationModel",
                                                      this->hwmMarkerModel);

  ui->quick_hwmMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::ColoredMarkers);
  this->mapFunctions->setMapTypes(ui->combo_hwmMaptype);
  this->setupMarkerClasses(ui->quick_hwmMap);
  ui->combo_hwmMaptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->changeHwmMaptype();

  this->mapFunctions->setMapQmlFile(ui->quick_hwmMap);

  QObject *hwmItem = ui->quick_hwmMap->rootObject();
  QMetaObject::invokeMethod(hwmItem, "setMapLocation", Q_ARG(QVariant, -124.66),
                            Q_ARG(QVariant, 36.88), Q_ARG(QVariant, 1.69));

  Colors::changeButtonColor(ui->button_hwmcolor, QColor(11, 84, 255));
  Colors::changeButtonColor(ui->button_121linecolor, QColor(7, 145, 0));
  Colors::changeButtonColor(ui->button_reglinecolor, QColor(255, 0, 0));
  Colors::changeButtonColor(ui->button_boundlinecolor, QColor(0, 0, 0));

  return;
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
  UpdateDialog *update = new UpdateDialog(this);
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

void MainWindow::on_button_usertimeseries_fitMarkers_clicked() {
  StationModel::fitMarkers(ui->quick_timeseriesMap, this->userDataStationModel);
  return;
}

void MainWindow::on_button_noaaDisplayValues_toggled(bool checked) {
  ui->noaa_graphics->setDisplayValues(checked);
}

void MainWindow::on_button_usgsDisplayValues_toggled(bool checked) {
  ui->usgs_graphics->setDisplayValues(checked);
}

void MainWindow::on_button_xtideDisplayValues_toggled(bool checked) {
  ui->xtide_graphics->setDisplayValues(checked);
}

void MainWindow::on_button_timeseriesDisplayValues_toggled(bool checked) {
  ui->timeseries_graphics->setDisplayValues(checked);
}

void MainWindow::on_button_hwmDisplayValues_toggled(bool checked) {
  ui->graphics_hwm->setDisplayValues(checked);
}

void MainWindow::on_button_refreshUsgsStations_clicked() {
  int n = this->mapFunctions->refreshMarkers(
      this->usgsStationModel, ui->quick_usgsMap, this->usgsMarkerLocations);
  this->stationDisplayWarning(n);
  return;
}

void MainWindow::on_button_refreshNoaaStations_clicked() {
  int n = this->mapFunctions->refreshMarkers(
      this->noaaStationModel, ui->quick_noaaMap, this->noaaMarkerLocations);
  this->stationDisplayWarning(n);
  return;
}

void MainWindow::on_button_refreshXtideStations_clicked() {
  int n = this->mapFunctions->refreshMarkers(
      this->xtideStationModel, ui->quick_xtideMap, this->xtideMarkerLocations);
  this->stationDisplayWarning(n);
  return;
}

void MainWindow::stationDisplayWarning(int n) {
  if (n > MAX_NUM_DISPLAYED_STATIONS) {
    QMessageBox::critical(this, "Error",
                          QString::number(n) +
                              " stations in current view (max: " +
                              QString::number(MAX_NUM_DISPLAYED_STATIONS) +
                              "). Zoom in and try again.");
  }
  return;
}

void MainWindow::on_combo_hwmMaptype_currentIndexChanged(int index) {
  Q_UNUSED(index);
  this->changeHwmMaptype();
}

void MainWindow::on_actionESRI_toggled(bool arg1) {
  if (!this->initialized) return;
  if (arg1) {
    ui->actionMapBox->setChecked(false);
    this->resetMapSource(MapFunctions::MapSource::ESRI);
  }
  if (!ui->actionESRI->isChecked() && !ui->actionMapBox->isChecked())
    ui->actionESRI->setChecked(true);
  return;
}

void MainWindow::on_actionMapBox_toggled(bool arg1) {
  if (!this->initialized) return;
  if (arg1) {
    if (this->mapFunctions->mapboxApiKey() == QString()) {
      QMessageBox::warning(
          this, "Missing API Key",
          "Please provide a MapBox API key by registering at mapbox.org");
      return;
    } else {
      ui->actionESRI->setChecked(false);
      this->resetMapSource(MapFunctions::MapSource::MapBox);
    }
  }
  if (!ui->actionESRI->isChecked() && !ui->actionMapBox->isChecked())
    ui->actionMapBox->setChecked(true);
  return;
}

void MainWindow::on_actionEnter_MapBox_API_Key_triggered() {
  QString apiKey = QInputDialog::getText(this, "Enter MapBox API Key",
                                         "Key: ", QLineEdit::Normal,
                                         this->mapFunctions->mapboxApiKey());
  if (apiKey != QString()) this->mapFunctions->setMapboxApiKey(apiKey);
  return;
}

void MainWindow::on_actionSave_Default_Map_Settings_triggered() {
  int mapIndex = 0;
  if (ui->MainTabs->currentIndex() == 0) {
    if (ui->subtab_livedata->currentIndex() == 0)
      mapIndex = ui->combo_noaa_maptype->currentIndex();
    else if (ui->subtab_livedata->currentIndex() == 1)
      mapIndex = ui->combo_user_maptype->currentIndex();
    else if (ui->subtab_livedata->currentIndex() == 2)
      mapIndex = ui->combo_xtide_maptype->currentIndex();
  } else if (ui->MainTabs->currentIndex() == 1) {
    mapIndex = ui->combo_user_maptype->currentIndex();
  } else if (ui->MainTabs->currentIndex() == 2) {
    mapIndex = ui->combo_hwmMaptype->currentIndex();
  }
  this->mapFunctions->setDefaultMapIndex(mapIndex);
  this->mapFunctions->saveDefaultMapTypeToDisk();
  return;
}
