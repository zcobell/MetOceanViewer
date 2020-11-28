#include "chartoptionsmenu.h"

#include "chartoptions.h"

#ifdef __APPLE__
const QString c_optionKey = QString::fromUtf8("\u2325");
const QString c_saveDataLabelString = "Save Data (" + c_optionKey + "+S)";
const QString c_saveGraphicLabelString = "Save Graphic (" + c_optionKey + "+G)";
const QString c_fitMarkersLabelString = "Reset Map (" + c_optionKey + "+M)";
const QString c_resetChartLabelString =
    "Reset Chart View (" + c_optionKey + "+R)";
const QString c_showValuesLabelString =
    "Display Values (" + c_optionKey + "+D)";
const QString c_chartOptionsLabelString =
    "Chart Options (" + c_optionKey + "+C)";
#else
const QString c_saveDataLabelString = "Save Data";
const QString c_saveGraphicLabelString = "Save Graphic";
const QString c_fitMarkersLabelString = "Reset Map";
const QString c_resetChartLabelString = "Reset Chart View";
const QString c_showValuesLabelString = "Display Values";
const QString c_chartOptionsLabelString = "Chart Options";
#endif

ChartOptionsMenu::ChartOptionsMenu(bool displayValues, bool saveData,
                                   bool saveGraphic, bool fitMarkers,
                                   bool resetChart, bool options,
                                   QWidget *parent)
    : QPushButton(parent) {
  this->m_menu = new QMenu(this);

  this->m_titleFontsize = 16;
  this->m_axisFontsize = 12;
  this->m_legendFontsize = 12;
  this->m_dateFormat = "auto";

  if (saveData) {
    this->m_saveData = new QAction(c_saveDataLabelString, this);
    this->m_saveData->setShortcut(QKeySequence(Qt::ALT + Qt::Key_S));
    this->m_menu->addAction(this->m_saveData);
    connect(this->m_saveData, SIGNAL(triggered()), this,
            SLOT(emitSaveDataSignal()));
  }

  if (saveGraphic) {
    this->m_saveGraphic = new QAction(c_saveGraphicLabelString, this);
    this->m_saveGraphic->setShortcut(QKeySequence(Qt::ALT + Qt::Key_G));
    this->m_menu->addAction(this->m_saveGraphic);
    connect(this->m_saveGraphic, SIGNAL(triggered()), this,
            SLOT(emitSaveGraphicSignal()));
  }

  if (saveData || saveGraphic) this->m_menu->addSeparator();

  if (fitMarkers) {
    this->m_fitMarkers = new QAction(c_fitMarkersLabelString, this);
    this->m_fitMarkers->setShortcut(QKeySequence(Qt::ALT + Qt::Key_M));
    this->m_menu->addAction(this->m_fitMarkers);
    connect(this->m_fitMarkers, SIGNAL(triggered()), this,
            SLOT(emitFitMarkersSignal()));
  }

  if (resetChart) {
    this->m_resetChart = new QAction(c_resetChartLabelString, this);
    this->m_resetChart->setShortcut(QKeySequence(Qt::ALT + Qt::Key_R));
    this->m_menu->addAction(this->m_resetChart);
    connect(this->m_resetChart, SIGNAL(triggered()), this,
            SLOT(emitResetChartSignal()));
  }

  if (displayValues) {
    this->m_displayValues = new QAction(c_showValuesLabelString, this);
    this->m_displayValues->setShortcut(QKeySequence(Qt::ALT + Qt::Key_D));
    this->m_displayValues->setCheckable(true);
    this->m_menu->addAction(this->m_displayValues);
    connect(this->m_displayValues, SIGNAL(triggered(bool)), this,
            SLOT(emitDisplayValuesSignal(bool)));
  }

  if (options) {
    this->m_options = new QAction(c_chartOptionsLabelString, this);
    this->m_options->setShortcut(QKeySequence(Qt::ALT + Qt::Key_C));
    this->m_menu->addAction(this->m_options);
    connect(this->m_options, SIGNAL(triggered()), this,
            SLOT(showChartOptions()));
  }

  this->setText("Options");
  this->setMenu(this->m_menu);
  connect(this, SIGNAL(clicked()), this, SLOT(showMenu()));
}

void ChartOptionsMenu::emitSaveDataSignal() { emit saveDataTriggered(); }

void ChartOptionsMenu::emitSaveGraphicSignal() { emit saveGraphicTriggered(); }

void ChartOptionsMenu::emitFitMarkersSignal() { emit fitMarkersTriggered(); }

void ChartOptionsMenu::emitResetChartSignal() { emit resetChartTriggered(); }

void ChartOptionsMenu::emitDisplayValuesSignal(bool b) {
  emit displayValuesTriggered(b);
}

void ChartOptionsMenu::showChartOptions() {
  ChartOptions *c =
      new ChartOptions(this->m_titleFontsize, this->m_axisFontsize,
                       this->m_legendFontsize, this->m_dateFormat, this);
  if (c->exec()) {
    if (c->titleFontsize() != this->m_titleFontsize ||
        c->axisFontsize() != this->m_axisFontsize ||
        c->legendFontsize() != this->m_legendFontsize ||
        this->m_dateFormat != c->dateFormat())
      this->m_titleFontsize = c->titleFontsize();
    this->m_axisFontsize = c->axisFontsize();
    this->m_legendFontsize = c->legendFontsize();
    this->m_dateFormat = c->dateFormat();
    emit chartOptionsChanged();
  }
  c->deleteLater();
}

void ChartOptionsMenu::emitChartOptionsChanged() { emit chartOptionsChanged(); }

QString ChartOptionsMenu::dateFormat() const { return m_dateFormat; }

int ChartOptionsMenu::legendFontsize() const { return m_legendFontsize; }

int ChartOptionsMenu::axisFontsize() const { return m_axisFontsize; }

int ChartOptionsMenu::titleFontsize() const { return m_titleFontsize; }
