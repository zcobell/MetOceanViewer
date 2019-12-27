#include "noaatab.h"

#include "noaacoops.h"

NoaaTab::NoaaTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::NOAA, stations, parent) {
  this->initialize();
}

void NoaaTab::plot() {
  this->chartview()->initializeAxis(1);
  Station s = this->mapWidget()->currentStation();
  QDateTime start = this->m_dte_startDate->dateTime();
  QDateTime end = this->m_dte_endDate->dateTime();
  NoaaCoOps *n =
      new NoaaCoOps(s, start, end, "water_level", "MSL", false, "metric", this);
  Hmdf *data = new Hmdf();
  int status = n->get(data);
  if (status != 0) {
    qDebug() << n->errorString();
    return;
  }

  qint64 dateMin, dateMax;
  double ymin, ymax;
  data->dataBounds(dateMin, dateMax, ymin, ymax);

  this->chartview()->chart()->setTitle(s.name());
  this->chartview()->setDateFormat(start, end);
  this->chartview()->setAxisLimits(start, end, ymin, ymax);
  QLineSeries *s1 = this->stationToSeries(data->station(0));
  s1->setName("name");
  s1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  this->chartview()->addSeries(s1, "series 1");
}

QGroupBox *NoaaTab::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);
  input->setTitle("NOAA Station Download Options");

  QHBoxLayout *sdLayout = new QHBoxLayout();
  QHBoxLayout *edLayout = new QHBoxLayout();
  QHBoxLayout *r1Layout = new QHBoxLayout();
  QHBoxLayout *r2Layout = new QHBoxLayout();
  QHBoxLayout *r3Layout = new QHBoxLayout();
  QHBoxLayout *tzLayout = new QHBoxLayout();
  QHBoxLayout *dataLayout = new QHBoxLayout();
  QHBoxLayout *datumlayout = new QHBoxLayout();
  QVBoxLayout *allLayout = new QVBoxLayout();

  this->m_lbl_startDate = new QLabel(this);
  this->m_lbl_endDate = new QLabel(this);
  this->m_dte_startDate = new QDateTimeEdit(this);
  this->m_dte_endDate = new QDateTimeEdit(this);
  this->m_btn_refresh = new QPushButton(this);
  this->m_btn_plot = new QPushButton(this);
  this->m_lbl_timezone = new QLabel(this);
  this->m_cbx_timezones = new QComboBox(this);
  this->m_lbl_datatype = new QLabel(this);
  this->m_cbx_datatype = new QComboBox(this);
  this->m_lbl_datum = new QLabel(this);
  this->m_cbx_datum = new QComboBox(this);

  this->m_lbl_startDate->setText("Start Time:");
  this->m_lbl_endDate->setText("End Time:");
  this->m_btn_refresh->setText("Refresh Stations");
  this->m_btn_plot->setText("Fetch Data");

  this->m_lbl_timezone->setText("Timezone:");
  this->m_cbx_timezones->addItems(timezoneList());

  this->m_lbl_datatype->setText("Product:");
  this->m_lbl_datum->setText("Datum:");

  this->m_cbx_datum->addItems(Datum::noaaDatumList());

  QDateTime startDate = QDateTime::currentDateTime().addDays(-1);
  QDateTime endDate = QDateTime::currentDateTime();

  this->m_dte_startDate->setDateTime(startDate);
  this->m_dte_endDate->setDateTime(endDate);

  this->m_dte_startDate->setMinimumWidth(160);
  this->m_dte_startDate->setMaximumWidth(160);
  this->m_dte_endDate->setMinimumWidth(160);
  this->m_dte_endDate->setMaximumWidth(160);
  this->m_cbx_timezones->setMinimumWidth(200);
  this->m_cbx_timezones->setMaximumWidth(200);
  this->m_cbx_datatype->setMinimumWidth(300);
  this->m_cbx_datatype->setMaximumWidth(300);
  this->m_cbx_datum->setMinimumWidth(100);
  this->m_cbx_datum->setMaximumWidth(100);

  connect(m_btn_refresh, SIGNAL(clicked()), this->mapWidget(),
          SLOT(refreshStations()));
  connect(m_btn_plot, SIGNAL(clicked()), this, SLOT(plot()));

  sdLayout->addWidget(m_lbl_startDate);
  sdLayout->addWidget(m_dte_startDate);
  edLayout->addWidget(m_lbl_endDate);
  edLayout->addWidget(m_dte_endDate);
  tzLayout->addWidget(m_lbl_timezone);
  tzLayout->addWidget(m_cbx_timezones);
  dataLayout->addWidget(m_lbl_datatype);
  dataLayout->addWidget(m_cbx_datatype);
  datumlayout->addWidget(m_lbl_datum);
  datumlayout->addWidget(m_cbx_datum);

  r1Layout->addLayout(sdLayout);
  r1Layout->addLayout(edLayout);
  r2Layout->addLayout(tzLayout);
  r2Layout->addWidget(m_btn_refresh);
  r2Layout->addWidget(m_btn_plot);
  r3Layout->addLayout(dataLayout);
  r3Layout->addLayout(datumlayout);

  r1Layout->addStretch();
  r2Layout->addStretch();
  r3Layout->addStretch();

  allLayout->addLayout(r1Layout);
  allLayout->addLayout(r2Layout);
  allLayout->addLayout(r3Layout);

  input->setLayout(allLayout);

  return input;
}
