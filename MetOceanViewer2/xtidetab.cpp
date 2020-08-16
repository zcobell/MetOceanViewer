#include "xtidetab.h"

#include "generic.h"
#include "xtidedata.h"

XTideTab::XTideTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::XTIDE, stations, parent) {
  this->initialize();
}

void XTideTab::connectSignals() {
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
  this->m_btn_compute = new QPushButton("Compute Tides", this);
  this->setCbx_mapType(new ComboBox("Map:", this));

  this->m_cbx_datum->combo()->addItems(Datum::vDatumList());
  this->m_cbx_units->combo()->addItems(QStringList() << "metric"
                                                     << "english");

  this->mapWidget()->mapFunctions()->setMapTypes(this->cbx_mapType()->combo());

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
  QHBoxLayout *r3 = new QHBoxLayout();

  r1->addWidget(this->startDateEdit());
  r1->addWidget(this->endDateEdit());
  r2->addWidget(this->m_cbx_datum);
  r2->addWidget(this->m_cbx_units);
  r2->addWidget(this->m_btn_compute);
  r3->addWidget(this->cbx_mapType());
  r3->addWidget(this->chartOptions());
  r1->addStretch();
  r2->addStretch();
  r3->addStretch();

  QVBoxLayout *v = new QVBoxLayout();
  v->addLayout(r1);
  v->addLayout(r2);
  v->addLayout(r3);
  v->addStretch();

  input->setLayout(v);
  input->setMinimumHeight(input->minimumSizeHint().height());
  input->setMaximumHeight(input->minimumSizeHint().height() + 10);
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
  this->data()->reset(new Hmdf());

  std::unique_ptr<XtideData> xtide(
      new XtideData(s, start, end, Generic::configDirectory()));
  ierr = xtide->get(this->data()->get(), datum);
  if (ierr != 0) {
    emit error(xtide->errorString());
    return;
  }

  QString unitString = "m";
  if (this->m_cbx_units->combo()->currentText() == "english") {
    unitString = "ft";
    for (size_t i = 0; i < this->data()->get()->station(0)->numSnaps(); ++i) {
      this->data()->get()->station(0)->setData(
          this->data()->get()->station(0)->data(i) * 3.28084, i);
    }
  }

  this->setPlotAxis(this->data()->get(), start, end, tzAbbrev,
                    Datum::datumName(datum), unitString, "Water Level");
  this->addSeriesToChart(this->data()->get(), tzOffset);
  this->chartview()->initializeAxisLimits();
  this->chartview()->initializeLegendMarkers();
  this->chartview()->chart()->setTitle("XTide: " + s.name());
  return;
}

void XTideTab::addSeriesToChart(Hmdf *data, const qint64 tzOffset) {
  QLineSeries *series =
      this->stationToSeries(this->data()->get()->station(0), tzOffset);
  series->setName(data->station(0)->name());
  series->setPen(
      QPen(QColor(Qt::green), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  this->chartview()->addSeries(series, data->station(0)->name());
}
