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
  this->m_chartView->m_chart = nullptr;
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

  if (this->m_chartView->m_chart != nullptr) delete this->m_chartView->m_chart;
  this->m_chartView->m_chart = new QChart();
  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);

  qint64 minDate, maxDate;
  double ymin, ymax;
  s->dataBounds(minDate, maxDate, ymin, ymax);
  QDateTime minDateTime = QDateTime::fromMSecsSinceEpoch(minDate);
  QDateTime maxDateTime = QDateTime::fromMSecsSinceEpoch(maxDate);

  QLineSeries *series1 = new QLineSeries(this);
  series1->setName(s->name());
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  //...Create the chart
  this->m_chartView->m_chart = new QChart();
  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);

  for (int j = 0; j < s->numSnaps(); j++) {
    if (QDateTime::fromMSecsSinceEpoch(s->date(j)).isValid()) {
      series1->append(s->date(j), s->data(j));
    }
  }

  this->m_chartView->m_chart->addSeries(series1);

  this->m_chartView->clear();
  this->m_chartView->addSeries(series1, s->name());

  QDateTimeAxis *axisX = new QDateTimeAxis(this);
  axisX->setTickCount(5);
  if (minDateTime.daysTo(maxDateTime) > 90)
    axisX->setFormat("MM/yyyy");
  else if (minDateTime.daysTo(maxDateTime) > 4)
    axisX->setFormat("MM/dd/yyyy");
  else
    axisX->setFormat("MM/dd/yyyy hh:mm");

  axisX->setMin(minDateTime);
  axisX->setMax(maxDateTime);
  this->m_chartView->m_chart->addAxis(axisX, Qt::AlignBottom);
  series1->attachAxis(axisX);

  QValueAxis *axisY = new QValueAxis(this);
  QString format;
  axisY->setLabelFormat(format);
  axisY->setTitleText("NDBC Station " + this->m_station.id());
  axisY->setMin(ymin);
  axisY->setMax(ymax);
  this->m_chartView->m_chart->addAxis(axisY, Qt::AlignLeft);
  series1->attachAxis(axisY);

  axisY->setTickCount(10);
  axisY->applyNiceNumbers();
  axisX->setGridLineColor(QColor(200, 200, 200));
  axisY->setGridLineColor(QColor(200, 200, 200));
  axisY->setShadesPen(Qt::NoPen);
  axisY->setShadesBrush(QBrush(QColor(240, 240, 240)));
  axisY->setShadesVisible(true);
  axisY->applyNiceNumbers();
  axisX->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
  axisY->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
//  this->m_chartView->m_chart->legend()->markers().at(0)->setFont(
//      QFont("Helvetica", 10, QFont::Bold));

  this->m_chartView->m_chart->setTitle(tr("USGS Station ") +
                                       this->m_station.id() + ": " +
                                       this->m_station.name());
  this->m_chartView->m_chart->setTitleFont(QFont("Helvetica", 14, QFont::Bold));
  m_chartView->setRenderHint(QPainter::Antialiasing);
  m_chartView->setChart(this->m_chartView->m_chart);

//  foreach (QLegendMarker *marker,
//           this->m_chartView->m_chart->legend()->markers()) {
//    // Disconnect possible existing connection to avoid multiple connections
//    QObject::disconnect(marker, SIGNAL(clicked()), this->m_chartView,
//                        SLOT(handleLegendMarkerClicked()));
//    QObject::connect(marker, SIGNAL(clicked()), this->m_chartView,
//                     SLOT(handleLegendMarkerClicked()));
//  }

  this->m_chartView->m_style = 1;
  this->m_chartView->m_coord =
      new QGraphicsSimpleTextItem(this->m_chartView->m_chart);
  this->m_chartView->m_coord->setPos(
      this->m_chartView->size().width() / 2 - 100,
      this->m_chartView->size().height() - 20);
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);

  return 0;
}
