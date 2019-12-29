#include "noaatab.h"

#include <array>
#include <string>

#include "metocean.h"
#include "noaacoops.h"
#include "noaaproductlist.h"

std::array<QColor, 2> c_noaaColors = {QColor(Qt::blue), QColor(Qt::green)};

NoaaTab::NoaaTab(QVector<Station> *stations, QWidget *parent)
    : MapChartWidget(TabType::NOAA, stations, parent) {
  this->initialize();
  this->connectSignals();
}

void NoaaTab::connectSignals() {
  connect(this->m_chartOptions, SIGNAL(displayValuesTriggered(bool)), this,
          SLOT(toggleDisplayValues(bool)));
  connect(this->m_chartOptions, SIGNAL(saveDataTriggered()), this,
          SLOT(saveData()));
  connect(this->m_chartOptions, SIGNAL(fitMarkersTriggered()), this,
          SLOT(fitMarkers()));
  connect(this->m_chartOptions, SIGNAL(resetChartTriggered()), this,
          SLOT(resetChart()));
  connect(this->m_chartOptions, SIGNAL(saveGraphicTriggered()), this,
          SLOT(saveGraphic()));
  connect(this->m_chk_vdatum, SIGNAL(clicked(bool)), this,
          SLOT(updateDatumList(bool)));
  connect(this->m_chk_activeOnly, SIGNAL(clicked(bool)), this,
          SLOT(refreshStations(bool)));
  connect(this->m_cbx_mapType->combo(), SIGNAL(currentIndexChanged(int)),
          this->mapWidget(), SLOT(changeMap(int)));
}

int NoaaTab::calculateDateInfo(QDateTime &startDate, QDateTime &endDate,
                               QDateTime &startDateGmt, QDateTime &endDateGmt,
                               QString &timezoneString, qint64 &tzOffset) {
  QString tzname = this->m_cbx_timezones->combo()->currentText();
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

std::pair<QString, bool> NoaaTab::getDatumParameters() {
  bool useVdatum;
  QString datumString;
  if (this->m_chk_vdatum->isChecked()) {
    datumString = "MSL";
    useVdatum = true;
  } else {
    datumString = this->m_cbx_datum->combo()->currentText();
    useVdatum = false;
  }
  return std::pair<QString, bool>(datumString, useVdatum);
}

int NoaaTab::getDataFromNoaa(const Station &s,
                             const NoaaProductList::NoaaProduct &product,
                             const QDateTime startDate, const QDateTime endDate,
                             const QString &datumString, Hmdf *data) {
  if (product.nProducts() == 1) {
    NoaaCoOps *n = new NoaaCoOps(
        s, startDate, endDate,
        QString::fromStdString(product.noaaDataString(0)), datumString, false,
        this->m_cbx_units->combo()->currentText(), this);
    int ierr = n->get(data);
    if (ierr != 0) {
      emit error(n->errorString());
      return 1;
    }
  } else {
    std::unique_ptr<NoaaCoOps> n1(new NoaaCoOps(
        s, startDate, endDate,
        QString::fromStdString(product.noaaDataString(0)), datumString, false,
        this->m_cbx_units->combo()->currentText()));
    std::unique_ptr<NoaaCoOps> n2(new NoaaCoOps(
        s, startDate, endDate,
        QString::fromStdString(product.noaaDataString(1)), datumString, false,
        this->m_cbx_units->combo()->currentText()));
    std::unique_ptr<Hmdf> noaaData1(new Hmdf());
    int ierr = n1->get(data);
    if (ierr != 0) {
      emit error(n1->errorString());
      return 1;
    }
    ierr = n2->get(noaaData1.get());
    if (ierr != 0) {
      emit error(n2->errorString());
      return 1;
    }
    data->addStation(noaaData1->station(0));
    data->station(1)->setParent(data);
    data->station(0)->setName(QString::fromStdString(product.seriesName(0)));
    data->station(1)->setName(QString::fromStdString(product.seriesName(1)));
  }
  if (data->nstations() < 1) {
    emit error("No station data was found");
    return 1;
  }
  return 0;
}

QString NoaaTab::getUnitsLabel(const NoaaProductList::NoaaProduct &p) {
  if (this->m_cbx_units->combo()->currentText() == "metric") {
    return QString::fromStdString(p.metricUnits());
  } else {
    return QString::fromStdString(p.englishUnits());
  }
}

void NoaaTab::performDatumTransformation(const Station &s, Hmdf *data) {
  Datum::VDatum d = Datum::datumID(this->m_cbx_datum->combo()->currentText());
  bool datumValid = data->applyDatumCorrection(s, d);
  if (!datumValid) {
    emit warning(
        "VDatum did not provide a valid datum converstaion at the specified "
        "station. Proceeding with MSL");
  }
}

void NoaaTab::setPlotAxis(Hmdf *data, const QDateTime &startDate,
                          const QDateTime &endDate, const QString &tzAbbrev,
                          const QString &datumString, const QString &unitString,
                          const QString &productName) {
  qint64 dateMin, dateMax;
  double ymin, ymax;
  data->dataBounds(dateMin, dateMax, ymin, ymax);
  this->chartview()->setDateFormat(startDate, endDate);
  this->chartview()->setAxisLimits(startDate, endDate, ymin, ymax);
  this->chartview()->dateAxis()->setTitleText("Date (" + tzAbbrev + ")");
  this->chartview()->yAxis()->setTitleText(productName + " (" + unitString +
                                           ", " + datumString + ")");
  return;
}

void NoaaTab::addSeriesToChart(Hmdf *data, const qint64 &tzOffset) {
  for (size_t i = 0; i < data->nstations(); ++i) {
    QLineSeries *series = this->stationToSeries(data->station(i), tzOffset);
    series->setName(data->station(i)->name());
    series->setPen(
        QPen(c_noaaColors[i], 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    this->chartview()->addSeries(series, data->station(i)->name());
  }
}

void NoaaTab::plot() {
  Station s = this->mapWidget()->currentStation();
  if (s.id() == "null") {
    emit error("No station was selected");
    return;
  };

  this->chartview()->clear();
  this->chartview()->initializeAxis(1);

  qint64 tzOffset;
  QDateTime start, end, startgmt, endgmt;
  QString tzAbbrev;
  int ierr =
      this->calculateDateInfo(start, end, startgmt, endgmt, tzAbbrev, tzOffset);
  if (ierr != 0) return;

  NoaaProductList::NoaaProduct p = this->m_noaaProductList.product(
      this->m_cbx_datatype->combo()->currentIndex());

  QString datumString;
  bool useVdatum;
  std::tie(datumString, useVdatum) = this->getDatumParameters();
  QString unitLabel = this->getUnitsLabel(p);

  this->m_noaaData.reset(new Hmdf());
  ierr = this->getDataFromNoaa(s, p, startgmt, endgmt, datumString,
                               this->m_noaaData.get());
  if (ierr != 0) return;

  if (useVdatum) this->performDatumTransformation(s, this->m_noaaData.get());

  this->setPlotAxis(this->m_noaaData.get(), start, end, tzAbbrev, datumString,
                    unitLabel, QString::fromStdString(p.axisLabel()));

  this->chartview()->chart()->setTitle(s.name());
  this->addSeriesToChart(this->m_noaaData.get(), tzOffset);
  this->chartview()->initializeAxisLimits();
  this->chartview()->initializeLegendMarkers();

  return;
}

void NoaaTab::updateDatumList(bool b) {
  if (b) {
    this->m_cbx_datum->combo()->clear();
    this->m_cbx_datum->combo()->addItems(Datum::vDatumList());
    this->m_cbx_datum->combo()->setCurrentText("MSL");
  } else {
    this->m_cbx_datum->combo()->clear();
    this->m_cbx_datum->combo()->addItems(Datum::noaaDatumList());
    this->m_cbx_datum->combo()->setCurrentText("MSL");
  }
}

QGroupBox *NoaaTab::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);
  input->setTitle("NOAA Station Download Options");

  QHBoxLayout *r1Layout = new QHBoxLayout();
  QHBoxLayout *r2Layout = new QHBoxLayout();
  QHBoxLayout *r3Layout = new QHBoxLayout();
  QVBoxLayout *allLayout = new QVBoxLayout();

  this->m_dte_startDate = new DateBox("Start Time: ", this);
  this->m_dte_endDate = new DateBox("End Time: ", this);
  this->m_btn_refresh = new QPushButton(this);
  this->m_btn_plot = new QPushButton(this);
  this->m_cbx_timezones = new ComboBox("Timezone: ", this);
  this->m_cbx_datatype = new ComboBox("Product: ", this);
  this->m_cbx_datum = new ComboBox("Datum: ", this);
  this->m_chk_vdatum = new QCheckBox(this);
  this->m_chk_activeOnly = new QCheckBox(this);
  this->m_cbx_units = new ComboBox("Units: ", this);
  this->m_chartOptions = new ChartOptionsMenu(this);
  this->m_cbx_mapType = new ComboBox("Map: ", this);

  this->m_btn_refresh->setText("Show Stations");
  this->m_btn_plot->setText("Plot");
  this->m_chk_vdatum->setText("Use VDatum");
  this->m_chk_activeOnly->setText("Active Stations Only");
  this->m_chk_activeOnly->setCheckState(Qt::Checked);
  this->m_chk_vdatum->setCheckState(Qt::Unchecked);

  this->m_cbx_timezones->combo()->addItems(timezoneList());
  this->m_cbx_datum->combo()->addItems(Datum::noaaDatumList());
  this->m_cbx_datatype->combo()->addItems(
      this->m_noaaProductList.productList());
  this->m_cbx_units->combo()->addItems(QStringList() << "metric"
                                                     << "english");
  this->mapWidget()->mapFunctions()->setMapTypes(this->m_cbx_mapType->combo());

  QDateTime startDate = QDateTime::currentDateTime().addDays(-1);
  QDateTime endDate = QDateTime::currentDateTime();

  this->m_dte_startDate->dateEdit()->setDateTime(startDate);
  this->m_dte_endDate->dateEdit()->setDateTime(endDate);

  this->m_dte_startDate->setMinimumWidth(160);
  this->m_dte_startDate->setMaximumWidth(160);
  this->m_dte_endDate->setMinimumWidth(160);
  this->m_dte_endDate->setMaximumWidth(160);
  this->m_cbx_timezones->combo()->setMinimumWidth(200);
  this->m_cbx_timezones->combo()->setMaximumWidth(200);
  this->m_cbx_datatype->combo()->setMinimumWidth(400);
  this->m_cbx_datatype->combo()->setMaximumWidth(400);
  this->m_cbx_datum->combo()->setMinimumWidth(100);
  this->m_cbx_datum->combo()->setMaximumWidth(100);

  this->m_cbx_timezones->combo()->setCurrentText("GMT");
  this->m_cbx_datum->combo()->setCurrentText("MSL");

  connect(m_btn_refresh, SIGNAL(clicked()), this->mapWidget(),
          SLOT(refreshStations()));
  connect(m_btn_plot, SIGNAL(clicked()), this, SLOT(plot()));

  r1Layout->addLayout(this->m_dte_startDate->layout());
  r1Layout->addLayout(this->m_dte_endDate->layout());
  r1Layout->addWidget(this->m_chk_activeOnly);
  r2Layout->addLayout(this->m_cbx_datatype->layout());
  r2Layout->addLayout(this->m_cbx_datum->layout());
  r2Layout->addWidget(this->m_chk_vdatum);
  r3Layout->addLayout(this->m_cbx_timezones->layout());
  r3Layout->addLayout(this->m_cbx_units->layout());
  r3Layout->addLayout(this->m_cbx_mapType->layout());
  r3Layout->addWidget(this->m_btn_plot);
  r3Layout->addWidget(this->m_btn_refresh);
  r3Layout->addWidget(this->m_chartOptions);

  r1Layout->addStretch();
  r2Layout->addStretch();
  r3Layout->addStretch();

  allLayout->addLayout(r1Layout);
  allLayout->addLayout(r2Layout);
  allLayout->addLayout(r3Layout);

  allLayout->addStretch();
  allLayout->setSpacing(0);
  allLayout->setContentsMargins(5, 0, 5, 0);

  input->setMaximumHeight(125);
  input->setMinimumHeight(125);
  input->setLayout(allLayout);

  return input;
}

void NoaaTab::refreshStations(bool b) {
  this->mapWidget()->refreshStations(true, b);
}

void NoaaTab::saveData() {
  if (this->m_noaaData) this->writeData(this->m_noaaData.get());
}
