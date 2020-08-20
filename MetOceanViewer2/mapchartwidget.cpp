#include "mapchartwidget.h"

#include <QDateTimeEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QTimeZone>
#include <QVBoxLayout>

#include "chartview.h"
#include "mapview.h"
#include "metocean.h"
#include "movStation.h"
#include "noaacoops.h"

MapChartWidget::MapChartWidget(TabType type, QVector<MovStation> *stations,
                               QWidget *parent)
    : QWidget(parent),
      m_stations(stations),
      m_type(type),
      m_chartOptions(nullptr),
      m_dte_startDate(nullptr),
      m_dte_endDate(nullptr),
      m_cbx_timezone(nullptr),
      m_mapFunctions(new MapFunctions()) {
  this->m_mapFunctions->getConfigurationFromDisk();
  this->m_mapFunctions->getMapboxKeyFromDisk();
}

void MapChartWidget::initialize() {
  this->m_windowLayout = new QVBoxLayout();
  this->m_mapLayout = this->generateMapChartLayout();
  this->m_inputBox = this->generateInputBox();
  this->m_windowLayout->addWidget(this->m_inputBox);
  this->m_windowLayout->addLayout(this->m_mapLayout);
  this->setAutoFillBackground(true);
  this->setLayout(this->m_windowLayout);
  this->connectSignals();
}

void MapChartWidget::connectSignals() {
  connect(this, SIGNAL(error(QString)), this, SLOT(showErrorDialog(QString)));
  connect(this, SIGNAL(warning(QString)), this,
          SLOT(showWarningDialog(QString)));
  connect(this->chartOptions(), SIGNAL(displayValuesTriggered(bool)), this,
          SLOT(toggleDisplayValues(bool)));
  connect(this->chartOptions(), SIGNAL(saveDataTriggered()), this,
          SLOT(saveData()));
  connect(this->chartOptions(), SIGNAL(fitMarkersTriggered()), this,
          SLOT(fitMarkers()));
  connect(this->chartOptions(), SIGNAL(resetChartTriggered()), this,
          SLOT(resetChart()));
  connect(this->chartOptions(), SIGNAL(saveGraphicTriggered()), this,
          SLOT(saveGraphic()));
  connect(this->chartOptions(), SIGNAL(chartOptionsChanged()), this,
          SLOT(chartOptionsChangeTriggered()));
  connect(this->cbx_mapType()->combo(), SIGNAL(currentIndexChanged(int)),
          this->mapWidget(), SLOT(changeMap(int)));
}

void MapChartWidget::plot() { return; }

void MapChartWidget::chartOptionsChangeTriggered() {
  this->chartview()->setTitleFontsize(this->m_chartOptions->titleFontsize());
  this->chartview()->setAxisFontsize(this->m_chartOptions->axisFontsize());
  this->chartview()->setLegendFontsize(this->m_chartOptions->legendFontsize());
  this->chartview()->setDateFormat(this->m_chartOptions->dateFormat());
}

void MapChartWidget::changeMapType(const MapFunctions::MapSource s) {
  this->mapWidget()->changeMapSource(s);
  this->m_mapFunctions->setMapSource(s);
  this->m_mapFunctions->setMapTypes(this->m_cbx_mapType->combo());
}

void MapChartWidget::saveGraphic() {
  QString filename = QFileDialog::getSaveFileName(
      this, "Save As...", MetOcean::lastDirectory(), "*.jpg");
  if (!filename.isNull()) {
    MetOcean::setLastDirectory(QFileInfo(filename).path());
    QFile outputFile(filename);
    QSize imageSize(
        this->m_mapWidget->size().width() + this->m_chartview->size().width(),
        this->m_mapWidget->size().height());
    QRect chartRect(this->m_mapWidget->size().width(), 0,
                    this->m_chartview->size().width(),
                    this->m_chartview->size().height());

    QImage pixmap(imageSize, QImage::Format_ARGB32);
    pixmap.fill(Qt::white);
    QPainter imagePainter(&pixmap);
    imagePainter.setRenderHints(QPainter::Antialiasing |
                                QPainter::TextAntialiasing |
                                QPainter::SmoothPixmapTransform);
    this->m_mapWidget->render(&imagePainter, QPoint(0, 0));
    this->m_chartview->render(&imagePainter, chartRect);

    outputFile.open(QIODevice::WriteOnly);
    pixmap.save(&outputFile, "JPG", 100);
  }
}

void MapChartWidget::saveData() {
  if (this->data()) {
    this->writeData(this->data()->get());
  }
}

void MapChartWidget::resetChart() { this->m_chartview->resetZoom(); }

void MapChartWidget::toggleDisplayValues(bool b) {
  this->m_chartview->setDisplayValues(b);
}

void MapChartWidget::fitMarkers() {}

void MapChartWidget::showErrorDialog(QString errorString) {
  QMessageBox::critical(this, "Error", errorString);
}

void MapChartWidget::showWarningDialog(QString warningString) {
  QMessageBox::warning(this, "Warning", warningString);
}

void MapChartWidget::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
    this->plot();
  } else {
    QWidget::keyPressEvent(event);
  }
}

MapFunctions *MapChartWidget::mapFunctions() const {
  return m_mapFunctions.get();
}

ComboBox *MapChartWidget::cbx_mapType() const { return m_cbx_mapType; }

void MapChartWidget::setCbx_mapType(ComboBox *cbx_mapType) {
  m_cbx_mapType = cbx_mapType;
}

ChartOptionsMenu *MapChartWidget::chartOptions() const {
  return m_chartOptions;
}

void MapChartWidget::setChartOptions(ChartOptionsMenu *chartOptions) {
  m_chartOptions = chartOptions;
}

void MapChartWidget::writeData(Hmdf::HmdfData *data) {
  QString filename = QFileDialog::getSaveFileName(
      this, "Save As...", MetOcean::lastDirectory(), "*.nc ;; *.imeds");
  if (!filename.isNull()) data->write(filename.toStdString());
}

QGroupBox *MapChartWidget::generateInputBox() { return nullptr; }

TabType MapChartWidget::type() const { return m_type; }

QLineSeries *MapChartWidget::stationToSeries(Hmdf::Station *s,
                                             const qint64 offset) {
  QLineSeries *series = new QLineSeries(this->m_chartview->chart());
  for (auto &t : *(s)) {
    series->append(t.date().toMSeconds() + offset, t.value());
    qDebug() << t.value();
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
  this->m_mapWidget =
      new MapView(this->m_stations, this->m_mapFunctions.get(), this);
  this->m_chartview = new ChartView(this);
  this->m_chartview->setObjectName("chart");

  QSizePolicy spLeft(QSizePolicy::Preferred, QSizePolicy::Expanding);
  QSizePolicy spRight(QSizePolicy::Preferred, QSizePolicy::Expanding);
  spLeft.setHorizontalStretch(1);
  spRight.setHorizontalStretch(1);

  this->m_mapWidget->setResizeMode(
      QQuickWidget::ResizeMode::SizeRootObjectToView);

  this->m_mapWidget->setSizePolicy(spLeft);
  this->m_chartview->setSizePolicy(spRight);

  this->m_chartview->setContentsMargins(0, 0, 0, 0);
  this->m_mapWidget->setContentsMargins(0, 0, 0, 0);

  layout->addWidget(this->m_mapWidget);
  layout->addWidget(this->m_chartview);
  layout->addSpacing(0);
  layout->setContentsMargins(0, 0, 0, 0);

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

int MapChartWidget::calculateDateInfo(QDateTime &startDate, QDateTime &endDate,
                                      QDateTime &startDateGmt,
                                      QDateTime &endDateGmt,
                                      QString &timezoneString,
                                      qint64 &tzOffset) {
  QTimeZone tz;
  if (this->m_cbx_timezone != nullptr) {
    tz = QTimeZone(this->m_cbx_timezone->combo()->currentText().toUtf8());
  } else {
    tz = QTimeZone("GMT");
  }

  startDate = this->startDateEdit()->dateEdit()->dateTime();
  endDate = this->endDateEdit()->dateEdit()->dateTime();

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

void MapChartWidget::refreshStations() {
  this->mapWidget()->refreshStations(true, false);
}

void MapChartWidget::setPlotAxis(
    Hmdf::HmdfData *data, const QDateTime &startDate, const QDateTime &endDate,
    const QString &tzAbbrev, const QString &datumString,
    const QString &unitString, const QString &productName) {
  Hmdf::Date dmin, dmax;
  double ymin, ymax;
  data->bounds(dmin, dmax, ymin, ymax);
  //  qint64 dateMin = dmin.toMSeconds();
  //  qint64 dateMax = dmax.toMSeconds();
  this->chartview()->setDateFormat(startDate, endDate);
  this->chartview()->setAxisLimits(startDate, endDate, ymin, ymax);
  this->chartview()->dateAxis()->setTitleText("Date (" + tzAbbrev + ")");

  if (datumString == QString()) {
    this->chartview()->yAxis()->setTitleText(productName + " (" + unitString +
                                             ")");
  } else {
    this->chartview()->yAxis()->setTitleText(productName + " (" + unitString +
                                             ", " + datumString + ")");
  }
  this->chartOptionsChangeTriggered();
  return;
}

DateBox *MapChartWidget::startDateEdit() { return this->m_dte_startDate; }

void MapChartWidget::setStartDateEdit(DateBox *widget) {
  this->m_dte_startDate = widget;
}

DateBox *MapChartWidget::endDateEdit() { return this->m_dte_endDate; }

void MapChartWidget::setEndDateEdit(DateBox *widget) {
  this->m_dte_endDate = widget;
}

ComboBox *MapChartWidget::timezoneCombo() { return this->m_cbx_timezone; }

void MapChartWidget::setTimezoneCombo(ComboBox *widget) {
  this->m_cbx_timezone = widget;
}

std::unique_ptr<Hmdf::HmdfData> *MapChartWidget::data() {
  return &this->m_data;
}

void MapChartWidget::changeBasemapEsri() {
  this->changeMapType(MapFunctions::MapSource::ESRI);
}

void MapChartWidget::changeBasemapOsm() {
  this->changeMapType(MapFunctions::MapSource::OSM);
}

void MapChartWidget::changeBasemapMapbox() {
  this->changeMapType(MapFunctions::MapSource::MapBox);
}
