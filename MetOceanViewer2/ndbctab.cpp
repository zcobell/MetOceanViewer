#include "ndbctab.h"

#include "ndbcdata.h"

NdbcTab::NdbcTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::NDBC, stations, parent), m_ready(false) {
  this->initialize();
}

void NdbcTab::connectSignals() {
  connect(this->m_cbx_product->combo(), SIGNAL(currentIndexChanged(int)), this,
          SLOT(draw(int)));
  connect(this->m_btn_fetchData, SIGNAL(clicked()), this, SLOT(plot()));
  MapChartWidget::connectSignals();
}

QGroupBox *NdbcTab::generateInputBox() {
  QGroupBox *box = new QGroupBox(this);
  box->setTitle("NDBC Stataion Download Options");

  this->m_btn_fetchData = new QPushButton(this);
  this->m_btn_fetchData->setText("Fetch Data");
  this->setStartDateEdit(new DateBox("Start Date:", this));
  this->setEndDateEdit(new DateBox("End Date:", this));
  this->setTimezoneCombo(new ComboBox("Timezone:", this));
  this->setCbx_mapType(new ComboBox("Map:", this));
  this->m_cbx_product = new ComboBox("Product:", this);
  this->setChartOptions(
      new ChartOptionsMenu(true, true, true, false, true, true, this));

  QDateTime startDate =
      QDateTime::currentDateTimeUtc().addYears(-1).addDays(-14);
  QDateTime endDate = QDateTime::currentDateTime().addYears(-1);

  this->startDateEdit()->dateEdit()->setDateTime(startDate);
  this->endDateEdit()->dateEdit()->setDateTime(endDate);

  this->mapWidget()->mapFunctions()->setMapTypes(this->cbx_mapType()->combo());
  this->timezoneCombo()->combo()->addItems(timezoneList());
  this->timezoneCombo()->combo()->setCurrentText("GMT");

  this->startDateEdit()->setMinimumWidth(160);
  this->startDateEdit()->setMaximumWidth(160);
  this->endDateEdit()->setMinimumWidth(160);
  this->endDateEdit()->setMaximumWidth(160);
  this->m_cbx_product->combo()->setMinimumWidth(200);
  this->m_cbx_product->combo()->setMaximumWidth(200);

  QHBoxLayout *r1 = new QHBoxLayout();
  r1->addLayout(this->startDateEdit()->layout());
  r1->addLayout(this->endDateEdit()->layout());
  r1->addWidget(this->m_btn_fetchData);

  QHBoxLayout *r2 = new QHBoxLayout();
  r2->addLayout(this->m_cbx_product->layout());
  r2->addLayout(this->cbx_mapType()->layout());

  QHBoxLayout *r3 = new QHBoxLayout();
  r3->addLayout(this->timezoneCombo()->layout());
  r3->addWidget(this->chartOptions());

  r1->addStretch();
  r1->addSpacing(0);
  r2->addStretch();
  r2->addSpacing(0);
  r3->addStretch();
  r3->addSpacing(0);

  QVBoxLayout *allLayout = new QVBoxLayout();
  allLayout->addLayout(r1);
  allLayout->addLayout(r2);
  allLayout->addLayout(r3);
  allLayout->addStretch();
  allLayout->addSpacing(0);
  allLayout->setContentsMargins(5, 0, 5, 5);

  box->setMaximumHeight(150);
  box->setMinimumHeight(150);
  box->setLayout(allLayout);

  return box;
}

void NdbcTab::plot() {
  this->m_ready = false;
  this->m_currentStation = this->mapWidget()->currentStation();
  if (this->m_currentStation.id() == "null") {
    emit error("No station was selected");
  }

  this->data()->reset(new Hmdf());
  NdbcData *ndbc = new NdbcData(
      this->m_currentStation, this->startDateEdit()->dateEdit()->dateTime(),
      this->endDateEdit()->dateEdit()->dateTime(), this);
  int ierr = ndbc->get(this->data()->get());
  if (ierr != 0) {
    emit error(ndbc->errorString());
    return;
  }

  this->m_cbx_product->combo()->clear();
  for (size_t i = 0; i < this->data()->get()->nstations(); ++i) {
    this->m_cbx_product->combo()->addItem(
        this->data()->get()->station(i)->name());
  }

  this->m_ready = true;
  this->draw(0);
}

void NdbcTab::draw(int index) {
  if (this->data() && this->m_ready) {
    this->chartview()->clear();
    this->chartview()->initializeAxis();

    qint64 tzOffset;
    QDateTime start, end, startgmt, endgmt;

    QString tzAbbrev;
    int ierr = this->calculateDateInfo(start, end, startgmt, endgmt, tzAbbrev,
                                       tzOffset);
    if (ierr != 0) return;

    QString unit = NdbcData::units(this->data()->get()->station(index)->name());

    this->setPlotAxis(this->data()->get(), start, end, tzAbbrev, QString(),
                      unit, this->data()->get()->station(index)->name());
    this->chartview()->chart()->setTitle(this->m_currentStation.name());
    this->addSeriesToChart(index, "NDBC" + this->m_currentStation.id(),
                           tzOffset);
    this->chartview()->initializeAxisLimits();
    this->chartview()->initializeLegendMarkers();
  }
}

void NdbcTab::addSeriesToChart(const int index, const QString &name,
                               const qint64 tzOffset) {
  QLineSeries *series =
      this->stationToSeries(this->data()->get()->station(index), tzOffset);
  series->setName(name);
  series->setPen(
      QPen(QColor(Qt::blue), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  this->chartview()->addSeries(series, name);
}
