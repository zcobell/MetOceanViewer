#include "noaatab.h"

#include <array>
#include <string>

#include "metocean.h"
#include "noaacoops.h"
#include "noaaproductlist.h"

std::array<QColor, 2> c_noaaColors = {QColor(Qt::blue), QColor(Qt::green)};

NoaaTab::NoaaTab(std::vector<MovStation> *stations, QWidget *parent)
    : MapChartWidget(TabType::NOAA, stations, parent) {
  this->initialize();
}

void NoaaTab::connectSignals() {
  connect(this->m_chk_vdatum, SIGNAL(clicked(bool)), this,
          SLOT(updateDatumList(bool)));
  connect(this->m_chk_activeOnly, SIGNAL(clicked(bool)), this,
          SLOT(refreshStations()));
  connect(this->m_btn_plot, SIGNAL(clicked()), this, SLOT(plot()));
  MapChartWidget::connectSignals();
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

int NoaaTab::getDataFromNoaa(const MovStation &s,
                             const NoaaProductList::NoaaProduct &product,
                             const QDateTime startDate, const QDateTime endDate,
                             const std::string &datumString,
                             Hmdf::HmdfData *data) {
  if (product.nProducts() == 1) {
    std::unique_ptr<NoaaCoOps> n(new NoaaCoOps(
        s, startDate, endDate, product.noaaDataString(0), datumString, false,
        this->m_cbx_units->combo()->currentText().toStdString()));
    int ierr = n->get(data);
    if (ierr != 0) {
      emit error(QString::fromStdString(n->errorString()));
      return 1;
    }
  } else {
    std::unique_ptr<NoaaCoOps> n1(new NoaaCoOps(
        s, startDate, endDate, product.noaaDataString(0), datumString, false,
        this->m_cbx_units->combo()->currentText().toStdString()));

    std::unique_ptr<NoaaCoOps> n2(new NoaaCoOps(
        s, startDate, endDate, product.noaaDataString(1), datumString, false,
        this->m_cbx_units->combo()->currentText().toStdString()));

    std::unique_ptr<Hmdf::HmdfData> noaaData1(new Hmdf::HmdfData());
    int ierr = n1->get(data);
    if (ierr != 0) {
      emit error(QString::fromStdString(n1->errorString()));
      return 1;
    }
    ierr = n2->get(noaaData1.get());
    if (ierr != 0) {
      emit error(QString::fromStdString(n2->errorString()));
      return 1;
    }
    data->addStation(*(noaaData1->station(0)));
    data->station(0)->setName(product.seriesName(0));
    data->station(1)->setName(product.seriesName(1));
  }
  if (data->nStations() < 1) {
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

void NoaaTab::performDatumTransformation(const MovStation &s,
                                         Hmdf::HmdfData *data) {
  Datum::VDatum d =
      Datum::datumID(this->m_cbx_datum->combo()->currentText().toStdString());

  if (d != Datum::NullDatum) {
    emit warning(
        "VDatum did not provide a valid datum converstaion at the specified "
        "station. Proceeding with MSL");
  } else {
    for (size_t i = 0; i < data->nStations(); ++i) {
      data->station(i)->shift(0, s.offset(d));
    }
  }
}

void NoaaTab::addSeriesToChart(Hmdf::HmdfData *data, const qint64 &tzOffset) {
  for (size_t i = 0; i < data->nStations(); ++i) {
    QLineSeries *series = this->stationToSeries(data->station(i), tzOffset);
    series->setName(QString::fromStdString(data->station(i)->name()));
    series->setPen(
        QPen(c_noaaColors[i], 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    this->chartview()->addSeries(
        series, QString::fromStdString(data->station(i)->name()));
  }
}

void NoaaTab::plot() {
  MovStation s = this->mapWidget()->currentStation();
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

  this->data()->reset(new Hmdf::HmdfData());
  ierr = this->getDataFromNoaa(s, p, startgmt, endgmt,
                               datumString.toStdString(), this->data()->get());
  if (ierr != 0) return;

  if (useVdatum) this->performDatumTransformation(s, this->data()->get());

  this->setPlotAxis(this->data()->get(), start, end, tzAbbrev, datumString,
                    unitLabel, QString::fromStdString(p.axisLabel()));

  this->chartview()->chart()->setTitle("NOAA Station " + s.id() + ": " +
                                       s.name());
  this->addSeriesToChart(this->data()->get(), tzOffset);
  this->chartview()->initializeAxisLimits();
  this->chartview()->initializeLegendMarkers();

  return;
}

void NoaaTab::updateDatumList(bool b) {
  if (b) {
    this->m_cbx_datum->combo()->clear();
    QStringList d;
    for (auto dd : Datum::vDatumList()) {
      d << QString::fromStdString(std::string(dd));
    }
    this->m_cbx_datum->combo()->addItems(d);
    this->m_cbx_datum->combo()->setCurrentText("MSL");
  } else {
    QStringList d;
    for (auto dd : Datum::noaaDatumList()) {
      d << QString::fromStdString(std::string(dd));
    }
    this->m_cbx_datum->combo()->clear();
    this->m_cbx_datum->combo()->addItems(d);
    this->m_cbx_datum->combo()->setCurrentText("MSL");
  }
}

QGroupBox *NoaaTab::generateInputBox() {
  QGroupBox *input = new QGroupBox(this);
  input->setTitle("NOAA Station Download Options");

  const size_t nLayoutRows = 4;
  this->m_rowLayouts.resize(nLayoutRows);
  for (auto &l : this->m_rowLayouts) {
    l = new QHBoxLayout();
  }

  QVBoxLayout *allLayout = new QVBoxLayout();

  this->setStartDateEdit(new DateBox("Start Time: ", this));
  this->setEndDateEdit(new DateBox("End Time: ", this));
  this->m_btn_plot = new QPushButton(this);
  this->setTimezoneCombo(new ComboBox("Timezone: ", this));
  this->m_cbx_datatype = new ComboBox("Product: ", this);
  this->m_cbx_datum = new ComboBox("Datum: ", this);
  this->m_chk_vdatum = new QCheckBox(this);
  this->m_chk_activeOnly = new QCheckBox(this);
  this->m_cbx_units = new ComboBox("Units: ", this);
  this->setCbx_mapType(new ComboBox("Map: ", this));
  this->setChartOptions(
      new ChartOptionsMenu(true, true, true, false, true, true, this));

  this->m_btn_plot->setText("Plot");
  this->m_chk_vdatum->setText("Use VDatum");
  this->m_chk_activeOnly->setText("Active Stations Only");
  this->m_chk_activeOnly->setCheckState(Qt::Checked);
  this->m_chk_vdatum->setCheckState(Qt::Unchecked);

  this->timezoneCombo()->combo()->addItems(timezoneList());
  QStringList d;
  for (auto dd : Datum::noaaDatumList()) {
    d << QString::fromStdString(std::string(dd));
  }
  this->m_cbx_datum->combo()->addItems(d);
  this->m_cbx_datatype->combo()->addItems(
      this->m_noaaProductList.productList());
  this->m_cbx_units->combo()->addItems(QStringList() << "metric"
                                                     << "english");
  this->mapWidget()->mapFunctions()->setMapTypes(this->cbx_mapType()->combo());

  QDateTime startDate = QDateTime::currentDateTimeUtc().addDays(-1);
  QDateTime endDate = QDateTime::currentDateTimeUtc();

  this->startDateEdit()->dateEdit()->setDateTime(startDate);
  this->endDateEdit()->dateEdit()->setDateTime(endDate);

  this->timezoneCombo()->combo()->setCurrentText("GMT");
  this->m_cbx_datum->combo()->setCurrentText("MSL");

  this->m_rowLayouts[0]->addWidget(this->startDateEdit());
  this->m_rowLayouts[0]->addWidget(this->endDateEdit());
  this->m_rowLayouts[0]->addWidget(this->m_chk_activeOnly);
  this->m_rowLayouts[0]->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  this->m_rowLayouts[1]->addWidget(this->m_cbx_datatype);
  this->m_rowLayouts[1]->addWidget(this->m_cbx_datum);
  this->m_rowLayouts[1]->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  this->m_rowLayouts[2]->addWidget(this->timezoneCombo());
  this->m_rowLayouts[2]->addWidget(this->m_cbx_units);
  this->m_rowLayouts[2]->addWidget(this->m_chk_vdatum);
  this->m_rowLayouts[2]->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));
  this->m_rowLayouts[3]->addWidget(this->cbx_mapType());
  this->m_rowLayouts[3]->addWidget(this->m_btn_plot);
  this->m_rowLayouts[3]->addWidget(this->chartOptions());
  this->m_rowLayouts[3]->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Preferred));

  for (auto l : this->m_rowLayouts) {
    l->addStretch();
    l->addSpacing(0);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSizeConstraint(QLayout::SetFixedSize);
    allLayout->addLayout(l);
  }

  allLayout->addStretch();
  allLayout->addSpacing(0);
  allLayout->setContentsMargins(0, 0, 0, 0);
  allLayout->addSpacerItem(
      new QSpacerItem(0, 0, QSizePolicy::Preferred, QSizePolicy::Expanding));

  input->setContentsMargins(0, 0, 0, 0);
  input->setLayout(allLayout);

  input->setMinimumHeight(input->minimumSizeHint().height());
  input->setMaximumHeight(input->minimumSizeHint().height() + 10);

  return input;
}

void NoaaTab::refreshStations() {
  bool b = this->m_chk_activeOnly->isChecked();
  this->mapWidget()->refreshStations(true, b);
}
