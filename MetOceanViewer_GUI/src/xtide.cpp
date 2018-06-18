/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#include "xtide.h"
#include <float.h>
#include "tideprediction.h"

//...Constructor
XTide::XTide(QQuickWidget *inMap, ChartView *inChart,
             QDateEdit *inStartDateEdit, QDateEdit *inEndDateEdit,
             QComboBox *inUnits, QStatusBar *inStatusBar,
             StationModel *inStationModel, QString *inCurrentStation,
             QObject *parent)
    : QObject(parent) {
  this->m_quickMap = inMap;
  this->m_chartView = inChart;
  this->m_startDateEdit = inStartDateEdit;
  this->m_endDateEdit = inEndDateEdit;
  this->m_comboUnits = inUnits;
  this->m_statusBar = inStatusBar;
  this->m_station.setLatitude(0.0);
  this->m_station.setLongitude(0.0);
  this->m_station.name() = QString();
  this->m_currentStation = inCurrentStation;
  this->m_stationModel = inStationModel;
  this->m_data = nullptr;
}

//...Destructor
XTide::~XTide() {}

QString XTide::getErrorString() { return this->m_errorString; }

ChartView *XTide::chartview() { return this->m_chartView; }

//...Overall routine for plotting XTide
int XTide::plotXTideStation() {
  int ierr;

  //...Get the executables
  ierr = this->findXTideExe();
  if (ierr != 0) return 1;

  //...Check if there is a station selected
  if (this->m_currentStation == QString()) return 1;

  //...Add to the environment
  qputenv("HFILE_PATH", this->m_harmfile.toUtf8());

  //...Get the selected station
  this->m_station =
      this->m_stationModel->findStation(*(this->m_currentStation));

  //...Calculate the tide signal
  ierr = this->calculateXTides();

  if (ierr == 0) this->plotChart();

  return 0;
}

QString XTide::getLoadedXTideStation() { return this->m_station.id(); }

QString XTide::getCurrentXTideStation() { return *(this->m_currentStation); }

//...Find the xTide executable
int XTide::findXTideExe() {
  QString installLocation =
      QApplication::applicationDirPath().replace(" ", "\ ");
  QString buildLocationLinux =
      QApplication::applicationDirPath() +
      "/../../MetOceanViewer/MetOceanViewer_GUI/mov_libs/bin";
  QString buildLocationWindows =
      QApplication::applicationDirPath() +
      "/../../../MetOceanViewer/thirdparty/xtide-2.15.1";
  QString appLocationMacOSX = QApplication::applicationDirPath();

  QFile location1(installLocation + "/harmonics.tcd");
  QFile location2(buildLocationLinux + "/harmonics.tcd");
  QFile location3(buildLocationWindows + "/harmonics.tcd");
  QFile location4(buildLocationLinux + "/harmonics.tcd");
  QFile location5(installLocation + "/harmonics.tcd");
  QFile location6(appLocationMacOSX + "/harmonics.tcd");

  if (location1.exists()) {
    this->m_harmfile = installLocation + "/harmonics.tcd";
    return 0;
  }

  if (location2.exists()) {
    this->m_harmfile = buildLocationLinux + "/harmonics.tcd";
    return 0;
  }

  if (location3.exists()) {
    this->m_harmfile = buildLocationWindows + "/harmonics.tcd";
    return 0;
  }

  if (location4.exists()) {
    this->m_harmfile = buildLocationLinux + "/harmonics.tcd";
    return 0;
  }

  if (location5.exists()) {
    this->m_harmfile = installLocation + "/harmonics.tcd";
    return 0;
  }

  if (location6.exists()) {
    this->m_harmfile = appLocationMacOSX + "/XTide/bin/harmonics.tcd";
    return 0;
  }

  emit xTideError(tr("The XTide harmonics database was not found"));

  return -1;
}

//...Compute the tidal signal
int XTide::calculateXTides() {
  int ierr;

  //...Get the selected dates
  QDateTime startDate = this->m_startDateEdit->dateTime();
  QDateTime endDate = this->m_endDateEdit->dateTime();
  startDate.setTime(QTime(0, 0, 0));
  endDate = endDate.addDays(1);
  endDate.setTime(QTime(0, 0, 0));

  QVector<qint64> date;
  QVector<double> data;
  ierr = TidePrediction::get(this->m_station.name(), startDate, endDate, 30,
                             date, data);

  if (this->m_data != nullptr) delete this->m_data;

  this->m_data = new Hmdf(this);

  HmdfStation *station = new HmdfStation(this->m_data);
  station->setName(this->m_station.name());
  station->setId(this->m_station.id());
  station->setCoordinate(this->m_station.coordinate());
  station->setIsNull(false);
  station->setStationIndex(0);
  station->setDate(date);
  station->setData(data);
  this->m_data->addStation(station);

  return ierr;
}

int XTide::plotChart() {
  double ymin, ymax;
  qint64 minDate, maxDate;
  QString format;

  this->m_data->dataBounds(minDate, maxDate, ymin, ymax);

  if (this->m_comboUnits->currentIndex() == 1)
    this->m_ylabel = tr("Water Surface Elevation (ft, MLLW)");
  else
    this->m_ylabel = tr("Water Surface Elevation (m, MLLW)");

  //...Create the chart
  this->m_chartView->m_chart = new QChart();

  QLineSeries *series1 = new QLineSeries(this);
  series1->setName(this->m_data->station(0)->name());
  series1->setPen(
      QPen(QColor(0, 255, 0), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  QDateTime minDateTime = this->m_startDateEdit->dateTime();
  QDateTime maxDateTime = this->m_endDateEdit->dateTime().addDays(1);
  minDateTime.setTime(QTime(0, 0, 0));
  maxDateTime.setTime(QTime(0, 0, 0));

  QDateTimeAxis *axisX = new QDateTimeAxis(this);
  axisX->setTickCount(5);
  if (minDateTime.daysTo(maxDateTime) > 90)
    axisX->setFormat("MM/yyyy");
  else if (minDateTime.daysTo(maxDateTime) > 4)
    axisX->setFormat("MM/dd/yyyy");
  else
    axisX->setFormat("MM/dd/yyyy hh:mm");
  axisX->setTitleText("Date (GMT)");
  axisX->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
  axisX->setMin(minDateTime);
  axisX->setMax(maxDateTime);
  this->m_chartView->m_chart->addAxis(axisX, Qt::AlignBottom);

  QValueAxis *axisY = new QValueAxis(this);
  axisY->setLabelFormat(format);
  axisY->setTitleText(this->m_ylabel);
  axisY->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
  axisY->setMin(ymin);
  axisY->setMax(ymax);
  this->m_chartView->m_chart->addAxis(axisY, Qt::AlignLeft);

  qDebug() << this->m_data->nstations();
  qDebug() << this->m_data->station(0)->numSnaps();

  for (int i = 0; i < this->m_data->station(0)->numSnaps(); i++)
    series1->append(this->m_data->station(0)->date(i),
                    this->m_data->station(0)->data(i));
  this->m_chartView->m_chart->addSeries(series1);
  this->m_chartView->clear();
  this->m_chartView->addSeries(series1, series1->name());
  series1->attachAxis(axisX);
  series1->attachAxis(axisY);
  axisY->setTickCount(10);
  axisY->applyNiceNumbers();
  axisX->setGridLineColor(QColor(200, 200, 200));
  axisY->setGridLineColor(QColor(200, 200, 200));
  axisY->setShadesPen(Qt::NoPen);
  axisY->setShadesBrush(QBrush(QColor(240, 240, 240)));
  axisY->setShadesVisible(true);

  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);
  this->m_chartView->m_chart->setTitle("XTide Station: " +
                                       this->m_station.name());
  this->m_chartView->m_chart->setTitleFont(QFont("Helvetica", 14, QFont::Bold));
  this->m_chartView->setRenderHint(QPainter::Antialiasing);
  this->m_chartView->setChart(this->m_chartView->m_chart);

  this->m_chartView->m_style = 1;
  this->m_chartView->m_coord =
      new QGraphicsSimpleTextItem(this->m_chartView->m_chart);
  this->m_chartView->m_coord->setPos(
      this->m_chartView->size().width() / 2 - 100,
      this->m_chartView->size().height() - 20);
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);
  this->m_chartView->m_chart->legend()->markers().at(0)->setFont(
      QFont("Helvetica", 10, QFont::Bold));

  foreach (QLegendMarker *marker,
           this->m_chartView->m_chart->legend()->markers()) {
    // Disconnect possible existing connection to avoid multiple connections
    QObject::disconnect(marker, SIGNAL(clicked()), this->m_chartView,
                        SLOT(handleLegendMarkerClicked()));
    QObject::connect(marker, SIGNAL(clicked()), this->m_chartView,
                     SLOT(handleLegendMarkerClicked()));
  }

  return 0;
}

int XTide::saveXTideData(QString filename, QString format) {
  if (this->m_data == nullptr) return 1;

  int ierr = this->m_data->write(filename);
  if (ierr != 0) {
    emit xTideError("Error writing XTide data to file.");
  }

  return 0;
}

int XTide::saveXTidePlot(QString filename, QString filter) {
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
