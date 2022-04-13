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

#include "mainwindow.h"
#include <QGeoRectangle>
#include <QGeoShape>
#include <QQmlContext>
#include <algorithm>
#include "aboutdialog.h"
#include "colors.h"
#include "crmsdialog.h"
#include "dflow.h"
#include "generic.h"
#include "keyhandler.h"
#include "mainwindow.h"
#include "mapfunctions.h"
#include "noaa.h"
#include "session.h"
#include "stationlocations.h"
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

  this->m_noaa = nullptr;
  this->m_usgs = nullptr;
  this->m_ndbc = nullptr;
  this->m_xtide = nullptr;
  this->m_userTimeseries = nullptr;
  this->m_hwm = nullptr;
  this->m_crms = nullptr;

  this->setupMetOceanViewerUI();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::setupMetOceanViewerUI() {
  this->mapActionGroup = new QActionGroup(this);
  this->mapActionGroup->addAction(ui->actionESRI);
  this->mapActionGroup->addAction(ui->actionMapBox);
  this->mapActionGroup->addAction(ui->actionOpenStreetMap);
  this->mapActionGroup->setExclusive(true);

  this->mapFunctions = new MapFunctions(this);
  this->mapFunctions->setMapSource(MapFunctions::MapSource::OSM);
  this->mapFunctions->getMapboxKeyFromDisk();
  this->mapFunctions->getConfigurationFromDisk();

  if (this->mapFunctions->mapboxApiKey() == QString()) {
    ui->actionMapBox->setEnabled(false);
  }

  if (this->mapFunctions->mapSource() == MapFunctions::MapSource::MapBox &&
      this->mapFunctions->mapboxApiKey() == QString()) {
    this->mapFunctions->setDefaultMapIndex(0);
    this->mapFunctions->setMapSource(MapFunctions::MapSource::OSM);
  }

  if (this->mapFunctions->mapSource() == MapFunctions::MapSource::ESRI) {
    ui->actionESRI->setChecked(true);
    ui->actionMapBox->setChecked(false);
    ui->actionOpenStreetMap->setChecked(false);
  } else if (this->mapFunctions->mapSource() ==
             MapFunctions::MapSource::MapBox) {
    ui->actionESRI->setChecked(false);
    ui->actionMapBox->setChecked(true);
    ui->actionOpenStreetMap->setChecked(false);
  } else if (this->mapFunctions->mapSource() == MapFunctions::MapSource::OSM) {
    ui->actionESRI->setChecked(false);
    ui->actionMapBox->setChecked(false);
    ui->actionOpenStreetMap->setChecked(true);
  }

  this->noaaStationModel = nullptr;
  this->usgsStationModel = nullptr;
  this->xtideStationModel = nullptr;
  this->userDataStationModel = nullptr;
  this->hwmMarkerModel = nullptr;
  this->ndbcStationModel = nullptr;
  this->crmsStationModel = nullptr;

  this->setupNoaaMap();
  this->setupUsgsMap();
  this->setupXTideMap();
  this->setupNdbcMap();
  this->setupUserTimeseriesMap();
  this->setupHighWaterMarkMap();
  this->setupTimeseriesTable();
  this->setupCrmsMap();

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

void MainWindow::resetMapSource(MapFunctions::MapSource source) {
  this->mapFunctions->setDefaultMapIndex(0);
  this->mapFunctions->setMapSource(source);

  this->setupNoaaMap();
  this->setupUsgsMap();
  this->setupXTideMap();
  this->setupNdbcMap();
  this->setupCrmsMap();
  this->setupUserTimeseriesMap();
  this->setupHighWaterMarkMap();

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

  if (LoadFile.isEmpty()) return;

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
  if (SaveFile.isEmpty()) {
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
      on_button_fetchndbc_clicked();
    } else if (ui->subtab_livedata->currentIndex() == 3) {
      on_button_xtide_compute_clicked();
    } else if (ui->subtab_livedata->currentIndex() == 4) {
      on_button_fetchcrms_clicked();
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

void MainWindow::changeNdbcMarker(QString markerId) {
  this->ndbcSelectedStation = markerId;
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

void MainWindow::changeCrmsMarker(QString markerId) {
  this->crmsSelectedStations = markerId;

  if (ui->check_crmsLockDates->isChecked()) return;

  Station s = this->crmsStationModel->findStation(markerId);
  ui->date_crmsStarttime->setMinimumDateTime(s.startValidDate());
  ui->date_crmsEndtime->setMaximumDateTime(s.endValidDate());

  if (ui->date_crmsStarttime->dateTime() < s.startValidDate()) {
    ui->date_crmsStarttime->setDateTime(s.startValidDate());
  }
  if (ui->date_crmsStarttime->dateTime() > s.endValidDate()) {
    ui->date_crmsStarttime->setDateTime(s.startValidDate());
  }

  if (ui->date_crmsEndtime->dateTime() > s.endValidDate()) {
    ui->date_crmsEndtime->setDateTime(s.endValidDate());
  }
  if (ui->date_crmsEndtime->dateTime() < s.startValidDate()) {
    ui->date_crmsEndtime->setDateTime(s.endValidDate());
  }

  if (ui->date_crmsEndtime->dateTime() < ui->date_crmsStarttime->dateTime()) {
    ui->date_crmsEndtime->setDateTime(
        ui->date_crmsEndtime->dateTime().addDays(7));
  }

  return;
}

void MainWindow::changeNoaaMaptype() {
  this->mapFunctions->setMapType(ui->combo_noaa_maptype->currentIndex(),
                                 ui->quick_noaaMap);
  return;
}

void MainWindow::changeNdbcMaptype() {
  this->mapFunctions->setMapType(ui->combo_ndbc_maptype->currentIndex(),
                                 ui->quick_ndbcMap);
  return;
}

void MainWindow::changeUsgsMaptype() {
  this->mapFunctions->setMapType(ui->combo_usgs_maptype->currentIndex(),
                                 ui->quick_usgsMap);
  return;
}

void MainWindow::changeXtideMaptype() {
  this->mapFunctions->setMapType(ui->combo_xtide_maptype->currentIndex(),
                                 ui->quick_xtideMap);
  return;
}

void MainWindow::changeUserMaptype() {
  this->mapFunctions->setMapType(ui->combo_user_maptype->currentIndex(),
                                 ui->quick_timeseriesMap);
  return;
}

void MainWindow::changeHwmMaptype() {
  this->mapFunctions->setMapType(ui->combo_hwmMaptype->currentIndex(),
                                 ui->quick_hwmMap);
  return;
}

void MainWindow::changeCrmsMaptype() {
  this->mapFunctions->setMapType(ui->combo_crms_maptype->currentIndex(),
                                 ui->quick_crmsMap);
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
      "markerMode", MapViewerMarkerModes::SingleSelectWithDates);
  this->setupMarkerClasses(ui->quick_noaaMap);
  this->mapFunctions->setMapTypes(ui->quick_noaaMap, ui->combo_noaa_maptype);
  ui->combo_noaa_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_noaaMap);
  this->mapFunctions->setMapQmlFile(ui->quick_noaaMap);
  this->noaaMarkerLocations =
      StationLocations::readMarkers(StationLocations::NOAA);
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

  this->m_noaaDelayTimer = new QTimer(this);
  this->m_noaaDelayTimer->setInterval(150);
  connect(this->m_noaaDelayTimer, SIGNAL(timeout()), this,
          SLOT(refreshNoaaStations()));
  connect(ui->quick_noaaMap->rootObject(), SIGNAL(mapViewChanged()), this,
          SLOT(updateNoaaStations()));

  return;
}

void MainWindow::refreshNoaaStations() {
  this->m_noaaDelayTimer->stop();
  bool active = ui->check_noaaActiveOnly->isChecked();
  this->mapFunctions->refreshMarkers(this->noaaStationModel, ui->quick_noaaMap,
                                     this->noaaMarkerLocations, true, active);
}

void MainWindow::updateNoaaStations() {
  if (this->m_noaaDelayTimer->isActive()) {
    this->m_noaaDelayTimer->stop();
  }
  this->m_noaaDelayTimer->start(150);
}

void MainWindow::setupNdbcMap() {
  if (this->ndbcStationModel == nullptr) {
    this->ndbcStationModel = new StationModel(this);
  }
  ui->quick_ndbcMap->rootContext()->setContextProperty("stationModel",
                                                       this->ndbcStationModel);
  ui->quick_ndbcMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelect);
  this->setupMarkerClasses(ui->quick_ndbcMap);
  this->mapFunctions->setMapTypes(ui->quick_ndbcMap, ui->combo_ndbc_maptype);
  ui->combo_ndbc_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_ndbcMap);
  this->mapFunctions->setMapQmlFile(ui->quick_ndbcMap);
  this->ndbcMarkerLocations =
      StationLocations::readMarkers(StationLocations::NDBC);
  QObject *ndbcItem = ui->quick_ndbcMap->rootObject();
  QObject::connect(ndbcItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeNdbcMarker(QString)));
  ui->date_ndbcStarttime->setDateTime(
      QDateTime::currentDateTimeUtc().addDays(-1));
  ui->date_ndbcEndtime->setDateTime(QDateTime::currentDateTimeUtc());

  QMetaObject::invokeMethod(ndbcItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));

  this->mapFunctions->refreshMarkers(this->ndbcStationModel, ui->quick_ndbcMap,
                                     this->ndbcMarkerLocations, false, true);

  this->m_ndbcDelayTimer = new QTimer(this);
  this->m_ndbcDelayTimer->setInterval(150);
  connect(this->m_ndbcDelayTimer, SIGNAL(timeout()), this,
          SLOT(refreshNdbcStations()));
  connect(ui->quick_ndbcMap->rootObject(), SIGNAL(mapViewChanged()), this,
          SLOT(updateNdbcStations()));

  return;
}

void MainWindow::refreshNdbcStations() {
  this->m_ndbcDelayTimer->stop();
  this->mapFunctions->refreshMarkers(this->ndbcStationModel, ui->quick_ndbcMap,
                                     this->ndbcMarkerLocations, true, false);
}

void MainWindow::updateNdbcStations() {
  if (this->m_ndbcDelayTimer->isActive()) {
    this->m_ndbcDelayTimer->stop();
  }
  this->m_ndbcDelayTimer->start(150);
}

void MainWindow::setupUsgsMap() {
  if (this->usgsStationModel == nullptr) {
    this->usgsStationModel = new StationModel(this);
  }
  ui->Date_usgsStart->setDateTime(QDateTime::currentDateTime().addDays(-7));
  ui->Date_usgsEnd->setDateTime(QDateTime::currentDateTime());
  ui->Date_usgsStart->setMinimumDateTime(QDateTime(QDate(1900, 1, 1),QTime(0,0,0)));
  ui->Date_usgsEnd->setMinimumDateTime(QDateTime(QDate(1900, 1, 1),QTime(0,0,0)));
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
      StationLocations::readMarkers(StationLocations::USGS);
  this->mapFunctions->setMapTypes(ui->quick_usgsMap, ui->combo_usgs_maptype);
  ui->combo_usgs_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_usgsMap);
  this->mapFunctions->setMapQmlFile(ui->quick_usgsMap);
  QObject *usgsItem = ui->quick_usgsMap->rootObject();
  QObject::connect(usgsItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeUsgsMarker(QString)));
  QMetaObject::invokeMethod(usgsItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));

  this->m_usgsDelayTimer = new QTimer(this);
  this->m_usgsDelayTimer->setInterval(250);
  connect(this->m_usgsDelayTimer, SIGNAL(timeout()), this,
          SLOT(refreshUsgsStations()));
  connect(ui->quick_usgsMap->rootObject(), SIGNAL(mapViewChanged()), this,
          SLOT(updateUsgsStations()));

  return;
}

void MainWindow::refreshUsgsStations() {
  this->m_usgsDelayTimer->stop();
  this->mapFunctions->refreshMarkers(this->usgsStationModel, ui->quick_usgsMap,
                                     this->usgsMarkerLocations, true, false);
}

void MainWindow::updateUsgsStations() {
  if (this->m_usgsDelayTimer->isActive()) {
    this->m_usgsDelayTimer->stop();
  }
  this->m_usgsDelayTimer->start(150);
}

void MainWindow::setupCrmsMap() {
  bool hasCrms = CrmsData::inquireCrmsStatus(Generic::crmsDataFile());
  if (!hasCrms) {
    ui->subtab_livedata->setTabEnabled(4, false);
    return;
  }

  if (this->crmsStationModel == nullptr) {
    this->crmsStationModel = new StationModel(this);
  }
  ui->quick_crmsMap->rootContext()->setContextProperty("stationModel",
                                                       this->crmsStationModel);
  ui->quick_crmsMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::SingleSelectWithDates);
  this->setupMarkerClasses(ui->quick_crmsMap);
  this->mapFunctions->setMapTypes(ui->quick_crmsMap, ui->combo_crms_maptype);
  ui->combo_crms_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_crmsMap);
  this->mapFunctions->setMapQmlFile(ui->quick_crmsMap);
  this->crmsMarkerLocations =
      StationLocations::readMarkers(StationLocations::CRMS);

  if (this->crmsMarkerLocations.size() == 0) {
    ui->subtab_livedata->setTabEnabled(4, false);
  }

  CrmsData::readHeader(Generic::crmsDataFile(), this->crmsHeader);
  CrmsData::generateStationMapping(Generic::crmsDataFile(), this->crmsMapping);

  QObject *crmsItem = ui->quick_crmsMap->rootObject();
  QObject::connect(crmsItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeCrmsMarker(QString)));
  ui->date_crmsStarttime->setDateTime(
      QDateTime::currentDateTimeUtc().addDays(-7));
  ui->date_crmsEndtime->setDateTime(QDateTime::currentDateTimeUtc());

  QMetaObject::invokeMethod(crmsItem, "setMapLocation", Q_ARG(QVariant, -91.5),
                            Q_ARG(QVariant, 30.0), Q_ARG(QVariant, 6.25));

  this->mapFunctions->refreshMarkers(this->crmsStationModel, ui->quick_crmsMap,
                                     this->crmsMarkerLocations, false, true);

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
      StationLocations::readMarkers(StationLocations::XTIDE);
  this->mapFunctions->setMapTypes(ui->quick_xtideMap, ui->combo_xtide_maptype);
  ui->combo_xtide_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_xtideMap);
  this->mapFunctions->setMapQmlFile(ui->quick_xtideMap);
  QObject *xtideItem = ui->quick_xtideMap->rootObject();
  QObject::connect(xtideItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeXtideMarker(QString)));
  QMetaObject::invokeMethod(xtideItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));

  this->m_xtideDelayTimer = new QTimer(this);
  this->m_xtideDelayTimer->setInterval(150);
  connect(this->m_xtideDelayTimer, SIGNAL(timeout()), this,
          SLOT(refreshXTideStations()));
  connect(ui->quick_xtideMap->rootObject(), SIGNAL(mapViewChanged()), this,
          SLOT(updateXTideStations()));

  return;
}

void MainWindow::refreshXTideStations() {
  this->m_xtideDelayTimer->stop();
  this->mapFunctions->refreshMarkers(this->xtideStationModel,
                                     ui->quick_xtideMap,
                                     this->xtideMarkerLocations, true, false);
}

void MainWindow::updateXTideStations() {
  if (this->m_xtideDelayTimer->isActive()) {
    this->m_xtideDelayTimer->stop();
  }
  this->m_xtideDelayTimer->start(150);
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
  this->mapFunctions->setMapTypes(ui->quick_timeseriesMap,
                                  ui->combo_user_maptype);
  this->setupMarkerClasses(ui->quick_timeseriesMap);
  ui->combo_user_maptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_timeseriesMap);
  this->mapFunctions->setMapQmlFile(ui->quick_timeseriesMap);
  QObject *userTimeseriesItem = ui->quick_timeseriesMap->rootObject();
  QObject::connect(userTimeseriesItem, SIGNAL(markerChanged(QString)), this,
                   SLOT(changeUserMarker(QString)));

  ui->date_TimeseriesStartDate->setDateTime(
      QDateTime(QDate(1980, 1, 1), QTime(0, 0, 0)));
  ui->date_TimeseriesEndDate->setDateTime(
      QDateTime(QDate(2050, 1, 1), QTime(0, 0, 0)));

  QMetaObject::invokeMethod(userTimeseriesItem, "setMapLocation",
                            Q_ARG(QVariant, -124.66), Q_ARG(QVariant, 36.88),
                            Q_ARG(QVariant, 1.69));
  return;
}

void MainWindow::setupHighWaterMarkMap() {
  if (this->hwmMarkerModel == nullptr) {
    this->hwmMarkerModel = new StationModel(this);
  }

  ui->quick_hwmMap->rootContext()->setContextProperty("stationModel",
                                                      this->hwmMarkerModel);

  ui->quick_hwmMap->rootContext()->setContextProperty(
      "markerMode", MapViewerMarkerModes::ColoredMarkers);
  this->mapFunctions->setMapTypes(ui->quick_hwmMap, ui->combo_hwmMaptype);
  this->setupMarkerClasses(ui->quick_hwmMap);
  ui->combo_hwmMaptype->setCurrentIndex(
      this->mapFunctions->getDefaultMapIndex());
  this->mapFunctions->setMapType(this->mapFunctions->getDefaultMapIndex(),
                                 ui->quick_hwmMap);
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
  if (this->userDataStationModel == nullptr) return;
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
  ui->actionMapBox->setChecked(false);
  if (arg1) this->resetMapSource(MapFunctions::MapSource::ESRI);
  return;
}

void MainWindow::on_actionOpenStreetMap_toggled(bool arg1) {
  if (!this->initialized) return;
  if (arg1) this->resetMapSource(MapFunctions::MapSource::OSM);
  return;
}

void MainWindow::on_actionMapBox_toggled(bool arg1) {
  if (!this->initialized) return;
  if (arg1) this->resetMapSource(MapFunctions::MapSource::MapBox);
  return;
}

void MainWindow::on_actionEnter_MapBox_API_Key_triggered() {
  QString apiKey = QInputDialog::getText(this, "Enter MapBox API Key",
                                         "Key: ", QLineEdit::Normal,
                                         this->mapFunctions->mapboxApiKey());
  if (apiKey != QString()) {
    this->mapFunctions->setMapboxApiKey(apiKey);
    ui->actionMapBox->setEnabled(true);
  } else {
    ui->actionMapBox->setEnabled(false);
  }
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
  this->mapFunctions->saveConfigurationToDisk();
  return;
}

void MainWindow::on_actionGenerate_CRMS_Database_triggered() {
  QPointer<CrmsDialog> dialog = new CrmsDialog(this);
  dialog->exec();
  return;
}

void MainWindow::on_combo_crms_maptype_currentIndexChanged(int index) {
  Q_UNUSED(index);
  this->changeCrmsMaptype();
}

void MainWindow::on_button_crmsfilterStationAvailablity_toggled(bool checked) {
  if (!checked) {
    this->mapFunctions->refreshMarkers(this->crmsStationModel,
                                       ui->quick_crmsMap,
                                       this->crmsMarkerLocations, false, false);
  } else {
    this->filterCrmsStationsByDate();
  }
  return;
}

void MainWindow::filterCrmsStationsByDate() {
  QDateTime start = ui->date_crmsStarttime->dateTime();
  QDateTime end = ui->date_crmsEndtime->dateTime();
  start.setTime(QTime(0, 0, 0));
  end.setTime(QTime(0, 0, 0));
  end = end.addDays(1);
  this->mapFunctions->refreshMarkers(this->crmsStationModel, ui->quick_crmsMap,
                                     this->crmsMarkerLocations, start, end);
  return;
}

void MainWindow::on_date_crmsStarttime_dateChanged(const QDate &date) {
  Q_UNUSED(date);
  if (ui->button_crmsfilterStationAvailablity->isChecked()) {
    this->filterCrmsStationsByDate();
  }
  return;
}
void MainWindow::on_date_crmsEndtime_dateChanged(const QDate &date) {
  Q_UNUSED(date);
  if (ui->button_crmsfilterStationAvailablity->isChecked()) {
    this->filterCrmsStationsByDate();
  }
  return;
}

void MainWindow::on_check_noaaActiveOnly_toggled(bool checked) {
  Q_UNUSED(checked);
  this->updateNoaaStations();
}
