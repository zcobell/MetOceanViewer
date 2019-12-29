#include "chartoptionsmenu.h"

#ifdef __APPLE__
const QString c_AltKey = QString::fromUtf8("\u2325");
#else
const QString c_AltKey = QString("ALT");
#endif

const QString c_saveDataLabelString = "Save Data (" + c_AltKey + "+S)";
const QString c_saveGraphicLabelString = "Save Graphic (" + c_AltKey + "+G)";
const QString c_fitMarkersLabelString = "Reset Map (" + c_AltKey + "+M)";
const QString c_resetChartLabelString = "Reset Chart View (" + c_AltKey + "+R)";
const QString c_showValuesLabelString = "Display Values (" + c_AltKey + "+D)";

ChartOptionsMenu::ChartOptionsMenu(QWidget *parent) : QPushButton(parent) {
  this->m_menu = new QMenu(this);
  this->m_saveData = new QAction(c_saveDataLabelString, this);
  this->m_fitMarkers = new QAction(c_fitMarkersLabelString, this);
  this->m_resetChart = new QAction(c_resetChartLabelString, this);
  this->m_displayValues = new QAction(c_showValuesLabelString, this);
  this->m_saveGraphic = new QAction(c_saveGraphicLabelString, this);

  this->m_saveData->setShortcut(QKeySequence(Qt::ALT + Qt::Key_S));
  this->m_saveGraphic->setShortcut(QKeySequence(Qt::ALT + Qt::Key_G));
  this->m_fitMarkers->setShortcut(QKeySequence(Qt::ALT + Qt::Key_M));
  this->m_resetChart->setShortcut(QKeySequence(Qt::ALT + Qt::Key_R));
  this->m_displayValues->setShortcut(QKeySequence(Qt::ALT + Qt::Key_D));

  this->m_displayValues->setCheckable(true);

  this->m_menu->addAction(this->m_saveData);
  this->m_menu->addAction(this->m_saveGraphic);
  this->m_menu->addSeparator();
  this->m_menu->addAction(this->m_fitMarkers);
  this->m_menu->addAction(this->m_resetChart);
  this->m_menu->addAction(this->m_displayValues);

  this->setMenu(this->m_menu);
  this->setText("Options");

  connect(this->m_saveData, SIGNAL(triggered()), this,
          SLOT(emitSaveDataSignal()));
  connect(this->m_saveGraphic, SIGNAL(triggered()), this,
          SLOT(emitSaveGraphicSignal()));
  connect(this->m_fitMarkers, SIGNAL(triggered()), this,
          SLOT(emitFitMarkersSignal()));
  connect(this->m_resetChart, SIGNAL(triggered()), this,
          SLOT(emitResetChartSignal()));
  connect(this->m_displayValues, SIGNAL(triggered(bool)), this,
          SLOT(emitDisplayValuesSignal(bool)));
}

void ChartOptionsMenu::emitSaveDataSignal() { emit saveDataTriggered(); }

void ChartOptionsMenu::emitSaveGraphicSignal() { emit saveGraphicTriggered(); }

void ChartOptionsMenu::emitFitMarkersSignal() { emit fitMarkersTriggered(); }

void ChartOptionsMenu::emitResetChartSignal() { emit resetChartTriggered(); }

void ChartOptionsMenu::emitDisplayValuesSignal(bool b) {
  emit displayValuesTriggered(b);
}
