#include "ndbc.h"
#include <QDateTimeAxis>
#include <QLegendMarker>
#include <QValueAxis>
#include "chartview.h"
#include "ndbcdata.h"

Ndbc::Ndbc(QQuickWidget *inMap, ChartView *inChart, QComboBox *inProductBox,
           QDateTimeEdit *inStartDateEdit, QDateTimeEdit *inEndDateEdit,
           QStatusBar *instatusBar, StationModel *stationModel,
           QString *inSelectedStation, QObject *parent)
    : QObject(parent) {
  this->m_map = inMap;
  this->m_chartView = inChart;
  this->m_productBox = inProductBox;
  this->m_startDateEdit = inStartDateEdit;
  this->m_endDateEdit = inEndDateEdit;
  this->m_statusBar = instatusBar;
  this->m_stationModel = stationModel;
  this->m_selectedStation = inSelectedStation;
  this->m_data = nullptr;
}

int Ndbc::plotStation() {
  QDateTime startDate = this->m_startDateEdit->dateTime();
  QDateTime endDate = this->m_endDateEdit->dateTime();

  this->m_station =
      this->m_stationModel->findStation(*(this->m_selectedStation));

  NdbcData *n = new NdbcData(this->m_station, startDate, endDate, this);

  this->m_data = new Hmdf(this);
  int ierr = n->get(this->m_data);

  delete n;

  if (ierr != 0) return ierr;

  this->m_productBox->clear();
  for (int i = 0; i < this->m_data->nstations(); i++) {
    this->m_productBox->addItem(this->m_data->station(i)->name());
  }

  this->plot(0);

  return 0;
}

int Ndbc::replotStation(int index) { return this->plot(index); }

int Ndbc::plot(int index) {
  HmdfStation *s = this->m_data->station(index);

  this->m_chartView->clear();
  this->m_chartView->initializeAxis(1);

  qint64 minDate, maxDate;
  double ymin, ymax;
  s->dataBounds(minDate, maxDate, ymin, ymax);
  QDateTime minDateTime = QDateTime::fromMSecsSinceEpoch(minDate);
  QDateTime maxDateTime = QDateTime::fromMSecsSinceEpoch(maxDate);

  QLineSeries *series1 = new QLineSeries(this->m_chartView->chart());
  series1->setName(s->name());
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  for (int j = 0; j < s->numSnaps(); j++) {
    if (QDateTime::fromMSecsSinceEpoch(s->date(j)).isValid()) {
      series1->append(s->date(j), s->data(j));
    }
  }

  this->m_chartView->chart()->addSeries(series1);
  this->m_chartView->addSeries(series1, s->name());

  this->m_chartView->setDateFormat(minDateTime, maxDateTime);
  this->m_chartView->setAxisLimits(minDateTime, maxDateTime, ymin, ymax);

  this->m_chartView->yAxis()->setTitleText("Observed");
  this->m_chartView->dateAxis()->setTitleText("Date (GMT)");

  series1->attachAxis(this->m_chartView->dateAxis());
  series1->attachAxis(this->m_chartView->yAxis());

  this->m_chartView->chart()->setTitle("NDBC Station " + this->m_station.id());
  this->m_chartView->initializeLegendMarkers();
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);

  return 0;
}
