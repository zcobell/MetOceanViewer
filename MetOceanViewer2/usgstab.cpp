#include "usgstab.h"

#include "metocean.h"
#include "usgswaterdata.h"

UsgsTab::UsgsTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::USGS, stations, parent) {
  this->initialize();
  this->m_ready = false;
}

void UsgsTab::connectSignals() {
  connect(this->m_btn_refresh, SIGNAL(clicked()), this,
          SLOT(refreshStations()));
  connect(this->m_btn_fetch, SIGNAL(clicked()), this, SLOT(plot()));
  connect(this->m_cbx_mapType->combo(), SIGNAL(currentIndexChanged(int)),
          this->mapWidget(), SLOT(changeMap(int)));
  connect(this->m_cbx_product->combo(), SIGNAL(currentIndexChanged(int)), this,
          SLOT(replot(int)));

  MapChartWidget::connectSignals();
}

void UsgsTab::refreshStations() {
  this->mapWidget()->refreshStations(true, false);
}

QGroupBox *UsgsTab::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);
  input->setTitle(
      "Select a station and click fetch to generate list of available "
      "products");
  QVBoxLayout *v = new QVBoxLayout();

  this->m_dte_startDate = new DateBox("Start Date:", this);
  this->m_dte_endDate = new DateBox("End Date:", this);
  this->m_cbx_timezone = new ComboBox("Time zone:", this);
  this->m_cbx_product = new ComboBox("Product:", this);
  this->m_cbx_mapType = new ComboBox("Map:", this);
  this->m_btn_fetch = new QPushButton("Fetch Data", this);
  this->m_btn_refresh = new QPushButton("Refresh Stations", this);

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

  this->m_dte_startDate->dateEdit()->setDateTime(startDate);
  this->m_dte_endDate->dateEdit()->setDateTime(endDate);

  this->m_rowLayouts.resize(4);
  for (auto &r : this->m_rowLayouts) {
    r = new QHBoxLayout();
  }

  this->m_cbx_timezone->combo()->addItems(timezoneList());
  this->mapWidget()->mapFunctions()->setMapTypes(this->m_cbx_mapType->combo());

  this->m_cbx_timezone->combo()->setCurrentText("GMT");
  this->m_cbx_product->combo()->setMinimumWidth(350);

  this->m_rowLayouts[0]->addLayout(this->m_dte_startDate->layout());
  this->m_rowLayouts[0]->addLayout(this->m_dte_endDate->layout());
  this->m_rowLayouts[1]->addLayout(this->m_cbx_timezone->layout());
  this->m_rowLayouts[1]->addLayout(this->m_cbx_mapType->layout());
  this->m_rowLayouts[2]->addWidget(this->m_lbl_buttonGroup);
  this->m_rowLayouts[2]->addWidget(this->m_rbtn_historic);
  this->m_rowLayouts[2]->addWidget(this->m_rbtn_instant);
  this->m_rowLayouts[2]->addWidget(this->m_rbtn_daily);
  this->m_rowLayouts[2]->addWidget(this->m_btn_fetch);
  this->m_rowLayouts[3]->addLayout(this->m_cbx_product->layout());
  this->m_rowLayouts[3]->addWidget(this->m_btn_refresh);
  this->m_rowLayouts[3]->addWidget(this->chartOptions());

  for (auto &r : this->m_rowLayouts) {
    r->addStretch();
    v->addLayout(r);
  }

  v->addStretch();
  input->setLayout(v);
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
  this->m_data.reset(new Hmdf());
  UsgsWaterdata *usgs = new UsgsWaterdata(
      this->m_currentStation, this->m_dte_startDate->dateEdit()->dateTime(),
      this->m_dte_endDate->dateEdit()->dateTime(), type, this);
  int ierr = usgs->get(this->m_data.get());
  if (ierr != 0) {
    emit error(usgs->errorString());
    return;
  }

  this->m_cbx_product->combo()->clear();
  for (size_t i = 0; i < this->m_data->nstations(); ++i) {
    this->m_cbx_product->combo()->addItem(this->m_data->station(i)->name());
  }

  this->m_ready = true;
  this->replot(0);

  return;
}

int UsgsTab::calculateDateInfo(QDateTime &startDate, QDateTime &endDate,
                               QDateTime &startDateGmt, QDateTime &endDateGmt,
                               QString &timezoneString, qint64 &tzOffset) {
  QString tzname = this->m_cbx_timezone->combo()->currentText();
  QTimeZone tz(tzname.toUtf8());
  startDate = this->m_dte_startDate->dateEdit()->dateTime();
  endDate = this->m_dte_endDate->dateEdit()->dateTime();

  if (startDate >= endDate) {
    emit error("Dates are not in ascending order");
    return 1;
  }

  QDateTime dmy = QDateTime::currentDateTime();
  dmy.setTimeZone(tz);
  timezoneString = tz.abbreviation(startDate);
  tzOffset =
      MetOcean::localMachineOffsetFromUTC() - tz.offsetFromUtc(startDate);

  startDateGmt = startDate.addSecs(-MetOcean::localMachineOffsetFromUTC());
  endDateGmt = endDate.addSecs(-MetOcean::localMachineOffsetFromUTC());
  startDateGmt.setTimeSpec(Qt::UTC);
  endDateGmt.setTimeSpec(Qt::UTC);

  return 0;
}

void UsgsTab::setPlotAxis(Hmdf *data, const QDateTime &startDate,
                          const QDateTime &endDate, const QString &tzAbbrev,
                          const QString &unitString,
                          const QString &productName) {
  qint64 dateMin, dateMax;
  double ymin, ymax;
  data->dataBounds(dateMin, dateMax, ymin, ymax);
  this->chartview()->setDateFormat(startDate, endDate);
  this->chartview()->setAxisLimits(startDate, endDate, ymin, ymax);
  this->chartview()->dateAxis()->setTitleText("Date (" + tzAbbrev + ")");
  this->chartview()->yAxis()->setTitleText(productName + " (" + unitString +
                                           ")");
  this->chartOptionsChangeTriggered();
  return;
}

void UsgsTab::replot(int index) {
  if (this->m_data && this->m_ready) {
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
        this->splitUsgsProductName(this->m_data->station(index)->name());
    this->setPlotAxis(this->m_data.get(), start, end, tzAbbrev, unitString,
                      productName);
    this->chartview()->chart()->setTitle(this->m_currentStation.name());
    this->addSeriesToChart(index, "USGS"+this->m_currentStation.id(), tzOffset);
    this->chartview()->initializeAxisLimits();
    this->chartview()->initializeLegendMarkers();
  }
}

void UsgsTab::addSeriesToChart(const int index, const QString &name,
                               const qint64 tzOffset) {
  QLineSeries *series =
      this->stationToSeries(this->m_data->station(index), tzOffset);
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
