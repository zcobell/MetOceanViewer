#include "mainwindow.h"

#include <QInputDialog>
#include <QKeySequence>
#include <QPushButton>

#include "generic.h"
#include "newtabdialog.h"
#include "tabwidget.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  Generic::createConfigDirectory();
  this->m_mapFunctions.getConfigurationFromDisk();
  this->m_mapFunctions.getMapboxKeyFromDisk();

  //...Create a tab widget and add to the central widget
  this->m_tabWidget = new TabWidget(this);
  ui->centralwidget->layout()->addWidget(this->m_tabWidget);

  this->generateMenus();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::generateMenus() {
  this->m_menuFile = new QMenu(this);
  this->m_menuOptions = new QMenu(this);
  this->m_menuMaptypes = new QMenu(this);

  this->m_menuFile->setTitle("File");
  this->m_menuOptions->setTitle("Options");
  this->m_menuMaptypes->setTitle("Select Map Type");

  const QString optionKey = QString::fromUtf8("\u2325");

  this->m_actionNewTab = new QAction(this->m_menuFile);
  this->m_actionNewTab->setText("New Tab");
  this->m_actionNewTab->setShortcut(QKeySequence(Qt::ALT + Qt::Key_N));
  connect(this->m_actionNewTab, SIGNAL(triggered()), this->m_tabWidget,
          SLOT(addNewTab()));

  this->m_actionSelectMapEsri = new QAction(this->m_menuMaptypes);
  this->m_actionSelectMapEsri->setText("ESRI");
  this->m_actionSelectMapEsri->setCheckable(true);
  connect(this->m_actionSelectMapEsri, SIGNAL(triggered()), this->m_tabWidget,
          SLOT(changeBasemapEsri()));
  connect(this->m_actionSelectMapEsri, SIGNAL(triggered()), this,
          SLOT(setBasemapEsri()));

  this->m_actionSelectMapOsm = new QAction(this->m_menuMaptypes);
  this->m_actionSelectMapOsm->setText("OpenStreetMap");
  this->m_actionSelectMapOsm->setCheckable(true);
  connect(this->m_actionSelectMapOsm, SIGNAL(triggered()), this->m_tabWidget,
          SLOT(changeBasemapOsm()));
  connect(this->m_actionSelectMapOsm, SIGNAL(triggered()), this,
          SLOT(setBasemapOsm()));

  this->m_actionSelectMapMapbox = new QAction(this->m_menuMaptypes);
  this->m_actionSelectMapMapbox->setText("MapBox");
  this->m_actionSelectMapMapbox->setCheckable(true);
  connect(this->m_actionSelectMapMapbox, SIGNAL(triggered()), this->m_tabWidget,
          SLOT(changeBasemapMapbox()));
  connect(this->m_actionSelectMapMapbox, SIGNAL(triggered()), this,
          SLOT(setBasemapMapbox()));
  if (this->m_mapFunctions.mapboxApiKey() == "") {
    this->m_actionSelectMapMapbox->setEnabled(false);
  }

  this->m_actionSetMapboxKey = new QAction(this->m_menuMaptypes);
  this->m_actionSetMapboxKey->setText("Enter MapBox API Key");
  connect(this->m_actionSetMapboxKey, SIGNAL(triggered()), this,
          SLOT(setMapboxApiKey()));

  if (this->m_mapFunctions.mapSource() == MapFunctions::MapSource::ESRI) {
    this->m_actionSelectMapEsri->setChecked(true);
  } else if (this->m_mapFunctions.mapSource() == MapFunctions::MapSource::OSM) {
    this->m_actionSelectMapOsm->setChecked(true);
  } else if (this->m_mapFunctions.mapSource() ==
             MapFunctions::MapSource::MapBox) {
    this->m_actionSelectMapMapbox->setChecked(true);
  }

  this->m_actionSaveSettings = new QAction(this->m_menuFile);
  this->m_actionSaveSettings->setText("Save Settings");
  connect(this->m_actionSaveSettings, SIGNAL(triggered()), this,
          SLOT(saveSettings()));

  QActionGroup *mapActGroup = new QActionGroup(this);
  mapActGroup->addAction(this->m_actionSelectMapEsri);
  mapActGroup->addAction(this->m_actionSelectMapOsm);
  mapActGroup->addAction(this->m_actionSelectMapMapbox);
  this->m_menuMaptypes->addAction(this->m_actionSelectMapEsri);
  this->m_menuMaptypes->addAction(this->m_actionSelectMapOsm);
  this->m_menuMaptypes->addAction(this->m_actionSelectMapMapbox);
  this->m_menuMaptypes->addSeparator();
  this->m_menuMaptypes->addAction(this->m_actionSetMapboxKey);

  this->m_menuFile->addAction(this->m_actionNewTab);
  this->m_menuFile->addAction(this->m_actionSaveSettings);
  this->m_menuOptions->addMenu(this->m_menuMaptypes);

  this->menuBar()->addMenu(this->m_menuFile);
  this->menuBar()->addMenu(this->m_menuOptions);
}

void MainWindow::setMapboxApiKey() {
  QString text =
      QInputDialog::getText(this, "MapBox API Key", "Key:", QLineEdit::Normal,
                            this->m_mapFunctions.mapboxApiKey());
  if (text != QString()) {
    this->m_mapFunctions.setMapboxApiKey(text);
    this->m_actionSelectMapMapbox->setEnabled(true);
  }
  return;
}

void MainWindow::saveSettings() {
  this->m_mapFunctions.saveConfigurationToDisk();
  this->m_mapFunctions.saveMapboxKeyToDisk();
}

void MainWindow::setBasemapEsri() {
  this->m_mapFunctions.setMapSource(MapFunctions::MapSource::ESRI);
}

void MainWindow::setBasemapOsm() {
  this->m_mapFunctions.setMapSource(MapFunctions::MapSource::OSM);
}

void MainWindow::setBasemapMapbox() {
  this->m_mapFunctions.setMapSource(MapFunctions::MapSource::MapBox);
}
