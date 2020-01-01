#include "xtidetab.h"

#include "generic.h"
#include "xtidedata.h"

XTideTab::XTideTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::XTIDE, stations, parent) {
  this->initialize();
}

void XTideTab::connectSignals() {
  connect(this->m_btn_refresh, SIGNAL(clicked()), this,
          SLOT(refreshStations()));
  connect(this->m_btn_compute, SIGNAL(clicked()), this, SLOT(plot()));
  MapChartWidget::connectSignals();
}

void XTideTab::refreshStations() {
  this->mapWidget()->refreshStations(true, false);
}

QGroupBox *XTideTab::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);
  input->setTitle("Select a station to compute tides");

  this->setStartDateEdit(new DateBox("Start Date:", this));
  this->setEndDateEdit(new DateBox("End Date:", this));
  this->setChartOptions(
      new ChartOptionsMenu(true, true, true, false, true, true, this));
  this->m_cbx_datum = new ComboBox("Datum:", this);
  this->m_cbx_units = new ComboBox("Units:", this);
  this->m_btn_refresh = new QPushButton("Refresh Stations", this);
  this->m_btn_compute = new QPushButton("Compute Tides", this);

  this->m_cbx_datum->combo()->addItems(Datum::vDatumList());
  this->m_cbx_units->combo()->addItems(QStringList() << "metric"
                                                     << "english");

  QTime tm(0, 0, 0);
  QDateTime startDate = QDateTime::currentDateTimeUtc().addDays(-7);
  QDateTime endDate = QDateTime::currentDateTimeUtc().addDays(1);
  startDate.setTime(tm);
  endDate.setTime(tm);
  this->startDateEdit()->dateEdit()->setDateTime(startDate);
  this->endDateEdit()->dateEdit()->setDateTime(endDate);

  this->startDateEdit()->dateEdit()->setDisplayFormat("MM/dd/yyyy");
  this->endDateEdit()->dateEdit()->setDisplayFormat("MM/dd/yyyy");

  QHBoxLayout *r1 = new QHBoxLayout();
  QHBoxLayout *r2 = new QHBoxLayout();

  r1->addLayout(this->startDateEdit()->layout());
  r1->addLayout(this->endDateEdit()->layout());
  r2->addLayout(this->m_cbx_datum->layout());
  r2->addLayout(this->m_cbx_units->layout());
  r2->addWidget(this->m_btn_refresh);
  r2->addWidget(this->m_btn_compute);
  r2->addWidget(this->chartOptions());
  r1->addStretch();
  r2->addStretch();

  QVBoxLayout *v = new QVBoxLayout();
  v->addLayout(r1);
  v->addLayout(r2);
  v->addStretch();

  input->setLayout(v);
  input->setMinimumHeight(120);
  input->setMaximumHeight(120);
  return input;
}

void XTideTab::plot() {
  this->chartview()->clear();
  this->chartview()->initializeAxis();
  Station s = this->mapWidget()->currentStation();
  if (s.id() == "null") {
    emit error("No station was selected");
    return;
  }

  qint64 tzOffset;
  QDateTime start, end, startgmt, endgmt;
  QString tzAbbrev;
  int ierr =
      this->calculateDateInfo(start, end, startgmt, endgmt, tzAbbrev, tzOffset);
  if (ierr != 0) return;

  Datum::VDatum datum =
      Datum::datumID(this->m_cbx_datum->combo()->currentText());
  this->m_data.reset(new Hmdf());

  std::unique_ptr<XtideData> xtide(
      new XtideData(s, start, end, Generic::configDirectory()));
  ierr = xtide->get(this->m_data.get(), datum);
  if (ierr != 0) {
    emit error(xtide->errorString());
    return;
  }

  QString unitString = "meters";
  if (this->m_cbx_units->combo()->currentText() == "english") {
    unitString = "feet";
    for (size_t i = 0; i < this->m_data->station(0)->numSnaps(); ++i) {
      this->m_data->station(0)->setData(
          this->m_data->station(0)->data(i) * 3.28084, i);
    }
  }

  this->setPlotAxis(this->m_data.get(), start, end, tzAbbrev,
                    Datum::datumName(datum), unitString, "Water Level");
  this->addSeriesToChart(this->m_data.get(), tzOffset);
  this->chartview()->initializeAxisLimits();
  this->chartview()->initializeLegendMarkers();
  return;
}

void XTideTab::addSeriesToChart(Hmdf *data, const qint64 tzOffset) {
  QLineSeries *series =
      this->stationToSeries(this->m_data->station(0), tzOffset);
  series->setName(data->station(0)->name());
  series->setPen(
      QPen(QColor(Qt::green), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  this->chartview()->addSeries(series, data->station(0)->name());
}
