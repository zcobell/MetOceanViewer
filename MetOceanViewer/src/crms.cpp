#include "crms.h"
#include "crmsdata.h"

using namespace QtCharts;

Crms::Crms(QQuickWidget *inMap, ChartView *inChart,
           QDateTimeEdit *inStartDateEdit, QDateTimeEdit *inEndDateEdit,
           QComboBox *inProduct, QStatusBar *inStatusBar,
           StationModel *inStationModel, QString *inCurrentStation,
           QVector<QString> &header, QMap<QString, size_t> &mapping,
           QObject *parent)
    : QObject(parent) {
  this->m_quickMap = inMap;
  this->m_chartView = inChart;
  this->m_startDateEdit = inStartDateEdit;
  this->m_endDateEdit = inEndDateEdit;
  this->m_comboProduct = inProduct;
  this->m_statusBar = inStatusBar;
  this->m_station.setLatitude(0.0);
  this->m_station.setLongitude(0.0);
  this->m_station.name() = QString();
  this->m_currentStation = inCurrentStation;
  this->m_stationModel = inStationModel;
  this->m_header = header;
  this->m_map = mapping;
  this->m_data = nullptr;
  this->m_working = false;
}

Crms::~Crms() {}

QString Crms::getErrorString() { return this->m_errorString; }

ChartView *Crms::chartview() { return this->m_chartView; }

int Crms::plotStation() {
  this->m_working = true;
  this->m_station =
      this->m_stationModel->findStation(*(this->m_currentStation));

  this->getData();

  this->m_comboProduct->clear();
  for (size_t i = 0; i < this->m_data->nstations(); ++i) {
    QString productName = this->m_data->station(i)->name();
    this->m_comboProduct->addItem(productName);
  }

  if (this->m_data->nstations() > 0) this->plot(0);
  this->m_working = false;
  return 0;
}

int Crms::replot(size_t index) {
  if (this->m_working) return 0;
  if (this->m_data == nullptr) return 0;
  if (*(this->m_currentStation) != this->m_station.id()) {
    emit error("Station selection has changed. Please requery data.");
    return 0;
  }
  if (index < this->m_data->nstations()) {
    return this->plot(index);
  }
  return 1;
}

QString Crms::getLoadedStation() { return this->m_station.id(); }

QString Crms::getCurrentStation() { return *(this->m_currentStation); }

int Crms::getData() {
  if (this->m_data != nullptr) delete this->m_data;
  this->m_data = new Hmdf(this);
  QDateTime start = this->m_startDateEdit->dateTime();
  QDateTime end = this->m_endDateEdit->dateTime();
  start.setTime(QTime(0, 0, 0));
  end.setTime(QTime(0, 0, 0));
  end = end.addDays(1);
  CrmsData *c = new CrmsData(this->m_station, start, end, this->m_header,
                             this->m_map, Generic::crmsDataFile(), this);
  int ierr = c->get(this->m_data);
  delete c;
  return ierr;
}

int Crms::plot(size_t index) {
  if (this->m_data->station(index)->numSnaps() == 0) {
    emit error("No valid data found");
    return 1;
  }

  this->m_chartView->clear();
  this->m_chartView->initializeAxis();

  double ymin, ymax;
  qint64 minDate, maxDate;
  minDate = std::numeric_limits<qint64>::max();
  maxDate = -std::numeric_limits<qint64>::max();
  ymax = -std::numeric_limits<double>::max();
  ymin = std::numeric_limits<double>::max();
  this->m_data->station(index)->dataBounds(minDate, maxDate, ymin, ymax);

  QLineSeries *series = new QLineSeries(this);
  series->setName(this->m_station.name());
  series->setPen(
      QPen(QColor(0, 255, 0), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  QDateTime minDateTime = this->m_startDateEdit->dateTime();
  QDateTime maxDateTime = this->m_endDateEdit->dateTime().addDays(1);
  minDateTime.setTime(QTime(0, 0, 0));
  maxDateTime.setTime(QTime(0, 0, 0));

  this->m_chartView->setDateFormat(minDateTime, maxDateTime);
  this->m_chartView->setAxisLimits(minDateTime, maxDateTime, ymin, ymax);
  this->m_chartView->dateAxis()->setTitleText("Date (GMT)");
  this->m_chartView->yAxis()->setTitleText(
      this->m_data->station(index)->name());
  for (int i = 0; i < this->m_data->station(index)->numSnaps(); i++) {
    series->append(this->m_data->station(index)->date(i),
                   this->m_data->station(index)->data(i));
  }

  this->m_chartView->addSeries(series, series->name());
  this->m_chartView->chart()->setTitle("CRMS Station: " +
                                       this->m_station.name());
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->initializeLegendMarkers();
  this->m_chartView->setStatusBar(this->m_statusBar);

  return 0;
}

int Crms::saveData(QString filename, QString format) {
  size_t index = this->m_comboProduct->currentIndex();
  if (this->m_data->station(index)->isNull()) return 0;
  Hmdf d;
  d.addStation(this->m_data->station(index));
  d.write(filename);
  return 0;
}

int Crms::savePlot(QString filename, QString filter) {
  if (filter == "PDF (*.pdf)") {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPrinter::Letter);
    printer.setResolution(400);
    printer.setOrientation(QPrinter::Landscape);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filename);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.begin(&printer);

    //...Page 1 - Chart
    this->m_chartView->render(&painter);

    //...Page 2 - Map
    printer.newPage();
    QPixmap renderedMap = this->m_quickMap->grab();
    QPixmap mapScaled = renderedMap.scaledToWidth(printer.width());
    if (mapScaled.height() > printer.height())
      mapScaled = renderedMap.scaledToHeight(printer.height());
    int cw = (printer.width() - mapScaled.width()) / 2;
    int ch = (printer.height() - mapScaled.height()) / 2;
    painter.drawPixmap(cw, ch, mapScaled.width(), mapScaled.height(),
                       mapScaled);

    painter.end();
  } else if (filter == "JPG (*.jpg *.jpeg)") {
    QFile outputFile(filename);
    QSize imageSize(
        this->m_quickMap->size().width() + this->m_chartView->size().width(),
        this->m_quickMap->size().height());
    QRect chartRect(this->m_quickMap->size().width(), 0,
                    this->m_chartView->size().width(),
                    this->m_chartView->size().height());

    QImage pixmap(imageSize, QImage::Format_ARGB32);
    pixmap.fill(Qt::white);
    QPainter imagePainter(&pixmap);
    imagePainter.setRenderHints(QPainter::Antialiasing |
                                QPainter::TextAntialiasing |
                                QPainter::SmoothPixmapTransform);
    this->m_quickMap->render(&imagePainter, QPoint(0, 0));
    this->m_chartView->render(&imagePainter, chartRect);

    outputFile.open(QIODevice::WriteOnly);
    pixmap.save(&outputFile, "JPG", 100);
  }
  return 0;
}
