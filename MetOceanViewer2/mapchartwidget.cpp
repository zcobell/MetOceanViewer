#include "mapchartwidget.h"

#include <QDateTimeEdit>
#include <QLabel>
#include <QTimeZone>
#include <QVBoxLayout>

#include "chartview.h"
#include "mapview.h"
#include "noaacoops.h"

MapChartWidget::MapChartWidget(TabType type, QVector<Station> *stations,
                               QWidget *parent)
    : QWidget(parent), m_stations(stations), m_type(type) {}

void MapChartWidget::initialize() {
  QVBoxLayout *window = new QVBoxLayout();
  QHBoxLayout *maplayout = this->generateMapChartLayout();
  QGroupBox *inputBox = this->generateInputBox();
  window->addWidget(inputBox);
  window->addLayout(maplayout);
  this->setAutoFillBackground(true);
  this->setLayout(window);
}

void MapChartWidget::plot() { return; }

QGroupBox *MapChartWidget::generateInputBox() { return nullptr; }

TabType MapChartWidget::type() const { return m_type; }

QLineSeries *MapChartWidget::stationToSeries(HmdfStation *s) {
  QLineSeries *series = new QLineSeries(this->m_chartview->chart());
  for (size_t i = 0; i < s->numSnaps(); ++i) {
    series->append(s->date(i), s->data(i));
  }
  return series;
}

ChartView *MapChartWidget::chartview() const { return m_chartview; }

void MapChartWidget::setChartview(ChartView *chartview) {
  m_chartview = chartview;
}

MapView *MapChartWidget::mapWidget() const { return m_mapWidget; }

void MapChartWidget::setMapWidget(MapView *mapWidget) {
  m_mapWidget = mapWidget;
}

QHBoxLayout *MapChartWidget::generateMapChartLayout() {
  QHBoxLayout *layout = new QHBoxLayout();
  this->m_mapWidget = new MapView(this->m_stations, this);
  this->m_chartview = new ChartView(this);

  QSizePolicy spLeft(QSizePolicy::Preferred, QSizePolicy::Expanding);
  QSizePolicy spRight(QSizePolicy::Preferred, QSizePolicy::Expanding);
  spLeft.setHorizontalStretch(1);
  spRight.setHorizontalStretch(1);

  this->m_mapWidget->setResizeMode(
      QQuickWidget::ResizeMode::SizeRootObjectToView);

  this->m_mapWidget->setSizePolicy(spLeft);
  this->m_chartview->setSizePolicy(spRight);

  layout->addWidget(this->m_mapWidget);
  layout->addWidget(this->m_chartview);

  return layout;
}

QStringList MapChartWidget::timezoneList() {
  QStringList tz;
  QList<QByteArray> tzl = QTimeZone::availableTimeZoneIds();
  tz.reserve(tzl.size());
  for (auto t : tzl) {
    tz.push_back(QString(t));
  }
  return tz;
}
