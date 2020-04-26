#include "usgstab.h"

#include "metocean.h"
#include "usgswaterdata.h"

UsgsTab::UsgsTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::USGS, stations, parent) {
  this->initialize();
  this->m_ready = false;
}

void UsgsTab::connectSignals() {
  connect(this->m_btn_fetch, SIGNAL(clicked()), this, SLOT(plot()));
  connect(this->m_cbx_product->combo(), SIGNAL(currentIndexChanged(int)), this,
          SLOT(replot(int)));
  MapChartWidget::connectSignals();
}

QGroupBox *UsgsTab::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);
  input->setTitle(
      "Select a station and click fetch to generate list of available "
      "products");
  QVBoxLayout *v = new QVBoxLayout();

  this->setStartDateEdit(new DateBox("Start Date:", this));
  this->setEndDateEdit(new DateBox("End Date:", this));
  this->setTimezoneCombo(new ComboBox("Time zone:", this));
  this->m_cbx_product = new ComboBox("Product:", this);
  this->setCbx_mapType(new ComboBox("Map:", this));
  this->m_btn_fetch = new QPushButton("Fetch Data", this);

  this->m_lbl_buttonGroup = new QLabel("Data Type:", this);
  this->m_rbtn_historic = new QRadioButton("Historic Data", this);
  this->m_rbtn_instant = new QRadioButton("Instant Data", this);
  this->m_rbtn_daily = new QRadioButton("Daily Data", this);

  this->m_buttonGroup = new QButtonGroup(this);
  this->m_buttonGroup->addButton(this->m_rbtn_historic, 0);
  this->m_buttonGroup->addButton(this->m_rbtn_instant, 1);
  this->m_buttonGroup->addButton(this->m_rbtn_daily, 2);

  this->m_rbtn_historic->setChecked(true);

  this->setChartOptions(
      new ChartOptionsMenu(true, true, true, false, true, true, this));

  QDateTime startDate = QDateTime::currentDateTimeUtc().addDays(-7);
  QDateTime endDate = QDateTime::currentDateTimeUtc();

  this->startDateEdit()->dateEdit()->setDateTime(startDate);
  this->endDateEdit()->dateEdit()->setDateTime(endDate);

  this->m_rowLayouts.resize(4);
  for (auto &r : this->m_rowLayouts) {
    r = new QHBoxLayout();
  }

  this->timezoneCombo()->combo()->addItems(timezoneList());
  this->mapWidget()->mapFunctions()->setMapTypes(this->cbx_mapType()->combo());

  this->timezoneCombo()->combo()->setCurrentText("GMT");
  this->m_cbx_product->combo()->setMinimumWidth(350);

  this->m_rowLayouts[0]->addLayout(this->startDateEdit()->layout());
  this->m_rowLayouts[0]->addLayout(this->endDateEdit()->layout());
  this->m_rowLayouts[1]->addLayout(this->timezoneCombo()->layout());
  this->m_rowLayouts[1]->addLayout(this->cbx_mapType()->layout());
  this->m_rowLayouts[2]->addWidget(this->m_lbl_buttonGroup);
  this->m_rowLayouts[2]->addWidget(this->m_rbtn_historic);
  this->m_rowLayouts[2]->addWidget(this->m_rbtn_instant);
  this->m_rowLayouts[2]->addWidget(this->m_rbtn_daily);
  this->m_rowLayouts[2]->addWidget(this->m_btn_fetch);
  this->m_rowLayouts[3]->addLayout(this->m_cbx_product->layout());
  this->m_rowLayouts[3]->addWidget(this->chartOptions());

  for (auto &r : this->m_rowLayouts) {
    r->addStretch();
    v->addLayout(r);
  }

  v->addStretch();
  input->setLayout(v);
  input->setMinimumHeight(200);
  input->setMaximumHeight(200);

  return input;
}

int UsgsTab::getDatabaseType() { return this->m_buttonGroup->checkedId(); }

void UsgsTab::plot() {
  this->m_ready = false;
  this->m_currentStation = this->mapWidget()->currentStation();
  if (this->m_currentStation.id() == "null") {
    emit error("No station was selected");
    return;
  };

  int type = this->getDatabaseType();
  this->data()->reset(new Hmdf());
  UsgsWaterdata *usgs = new UsgsWaterdata(
      this->m_currentStation, this->startDateEdit()->dateEdit()->dateTime(),
      this->endDateEdit()->dateEdit()->dateTime(), type, this);
  int ierr = usgs->get(this->data()->get());
  if (ierr != 0) {
    emit error(usgs->errorString());
    return;
  }

  this->m_cbx_product->combo()->clear();
  for (size_t i = 0; i < this->data()->get()->nstations(); ++i) {
    this->m_cbx_product->combo()->addItem(
        this->data()->get()->station(i)->name());
  }

  this->m_ready = true;
  this->replot(0);

  return;
}

void UsgsTab::replot(int index) {
  if (this->data() && this->m_ready) {
    this->chartview()->clear();
    this->chartview()->initializeAxis();

    qint64 tzOffset;
    QDateTime start, end, startgmt, endgmt;

    QString tzAbbrev;
    int ierr = this->calculateDateInfo(start, end, startgmt, endgmt, tzAbbrev,
                                       tzOffset);
    if (ierr != 0) return;
    QString unitString, productName;
    std::tie(productName, unitString) =
        this->splitUsgsProductName(this->data()->get()->station(index)->name());
    this->setPlotAxis(this->data()->get(), start, end, tzAbbrev, QString(),
                      unitString, productName);
    this->chartview()->chart()->setTitle(this->m_currentStation.name());
    this->addSeriesToChart(index, "USGS" + this->m_currentStation.id(),
                           tzOffset);
    this->chartview()->initializeAxisLimits();
    this->chartview()->initializeLegendMarkers();
  }
}

void UsgsTab::addSeriesToChart(const int index, const QString &name,
                               const qint64 tzOffset) {
  QLineSeries *series =
      this->stationToSeries(this->data()->get()->station(index), tzOffset);
  series->setName(name);
  series->setPen(
      QPen(QColor(Qt::blue), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  this->chartview()->addSeries(series, name);
}

std::tuple<QString, QString> UsgsTab::splitUsgsProductName(
    const QString &product) {
  QString name, unit;
  unit = "unknown";
  QStringList split = product.split(",");
  name = split[0];
  if (split.size() > 1) unit = split.back().simplified();
  return std::tuple<QString, QString>(name, unit);
}

void UsgsTab::saveData() {
  std::unique_ptr<Hmdf> data(new Hmdf());
  data->setDatum(this->data()->get()->datum());
  data->setUnits(this->data()->get()->units());
  data->setHeader1(this->data()->get()->header1());
  data->setHeader2(this->data()->get()->header2());
  data->setHeader3(this->data()->get()->header3());
  data->addStation(this->data()->get()->station(
      this->m_cbx_product->combo()->currentIndex()));
  this->writeData(data.get());
  return;
}
