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
#include "usgs.h"
#include <QGeoRectangle>
#include <QGeoShape>

Usgs::Usgs(QQuickWidget *inMap, ChartView *inChart, QRadioButton *inDailyButton,
           QRadioButton *inHistoricButton, QRadioButton *inInstantButton,
           QComboBox *inProductBox, QDateEdit *inStartDateEdit,
           QDateEdit *inEndDateEdit, QStatusBar *instatusBar,
           QComboBox *inUSGSTimezoneLocation, QComboBox *inUSGSTimezone,
           StationModel *stationModel, QString *inSelectedStation,
           QObject *parent)
    : QObject(parent) {
  //...Initialize variables
  this->m_usgsDataReady = false;
  this->m_usgsBeenPlotted = false;
  this->m_currentStation.setName("none");
  this->m_currentStation.setId("none");
  this->m_productIndex = 0;
  this->m_productName = "none";
  this->m_usgsDataMethod = 0;
  this->m_currentStation.setLatitude(0.0);
  this->m_currentStation.setLongitude(0.0);
  this->m_stationModel = stationModel;
  this->m_selectedStation = inSelectedStation;

  //...Assign object pointers
  this->m_quickMap = inMap;
  this->m_chartView = inChart;
  this->m_buttonDaily = inDailyButton;
  this->m_buttonHistoric = inHistoricButton;
  this->m_buttonInstant = inInstantButton;
  this->m_comboProduct = inProductBox;
  this->m_startDateEdit = inStartDateEdit;
  this->m_endDateEdit = inEndDateEdit;
  this->m_statusBar = instatusBar;
  this->m_comboTimezoneLocation = inUSGSTimezoneLocation;
  this->m_comboTimezone = inUSGSTimezone;

  //...Initialize the timezone
  this->m_tz = new Timezone(this);
  this->m_tz->fromAbbreviation(
      this->m_comboTimezone->currentText(),
      static_cast<TZData::Location>(
          this->m_comboTimezoneLocation->currentIndex()));
  this->m_offsetSeconds = m_tz->utcOffset() * 1000;
  this->m_priorOffsetSeconds = this->m_offsetSeconds;
}

Usgs::~Usgs() {}

int Usgs::plotNewUSGSStation() {
  if (*(this->m_selectedStation) == "-1") {
    emit usgsError(tr("You must select a station"));
    return 1;
  } else {
    int ierr;

    this->m_currentStation =
        this->m_stationModel->findStation(*(this->m_selectedStation));

    //...Check the data type
    if (m_buttonInstant->isChecked())
      this->m_usgsDataMethod = 1;
    else if (m_buttonDaily->isChecked())
      this->m_usgsDataMethod = 2;
    else
      this->m_usgsDataMethod = 0;

    //...Set status bar
    this->m_statusBar->showMessage(tr("Downloading data from USGS..."));

    //...Wipe out the combo box
    this->m_comboProduct->clear();

    //...Get the time period for the data
    this->m_requestEndDate = m_endDateEdit->dateTime();
    this->m_requestStartDate = m_startDateEdit->dateTime();

    //...Grab the data from the server
    UsgsWaterdata *waterData =
        new UsgsWaterdata(this->m_currentStation, this->m_requestStartDate,
                          this->m_requestEndDate, this->m_usgsDataMethod, this);
    this->m_allStationData = new Hmdf(this);
    ierr = waterData->get(this->m_allStationData);
    this->m_statusBar->clearMessage();
    if (ierr != 0) {
      emit usgsError(waterData->errorString());
      return ierr;
    }

    //...Update combo box
    for (int i = 0; i < this->m_allStationData->nstations(); i++) {
      this->m_comboProduct->addItem(this->m_allStationData->station(i)->name());
    }

    this->m_usgsDataReady = true;

    //...Plot first series
    this->plotUSGS();
  }

  return 0;
}

int Usgs::replotCurrentUSGSStation(int index) {
  if (this->m_usgsDataReady) {
    this->m_productIndex = index;
    this->m_productName = m_comboProduct->currentText();
    int ierr = this->plotUSGS();
    this->m_statusBar->clearMessage();
    if (ierr != 0) {
      this->m_errorString = tr("No data available for this selection.");
      return ierr;
    }
    return 0;
  }
  return 0;
}

int Usgs::plotUSGS() {
  QString format;

  this->m_productIndex = this->m_comboProduct->currentIndex();
  this->m_productName = this->m_comboProduct->currentText();

  HmdfStation *station = this->m_allStationData->station(this->m_productIndex);

  if (station->numSnaps() < 5) return 1;

  //...Create the line series
  qint64 dateMin, dateMax;
  double ymin, ymax;
  station->dataBounds(dateMin, dateMax, ymin, ymax);

  QDateTime minDateTime, maxDateTime;
  minDateTime = QDateTime::fromMSecsSinceEpoch(dateMin);
  maxDateTime = QDateTime::fromMSecsSinceEpoch(dateMax);
  minDateTime.setTimeSpec(Qt::UTC);
  maxDateTime.setTimeSpec(Qt::UTC);

  minDateTime =
      QDateTime(minDateTime.date(), QTime(minDateTime.time().hour(), 0, 0));
  maxDateTime =
      QDateTime(maxDateTime.date(), QTime(maxDateTime.time().hour() + 1, 0, 0));

  QLineSeries *series1 = new QLineSeries(this);
  series1->setName(this->m_productName);
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  //...Create the chart
  this->m_chartView->m_chart = new QChart();
  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);

  for (int j = 0; j < station->numSnaps(); j++) {
    if (QDateTime::fromMSecsSinceEpoch(station->date(j)).isValid()) {
      series1->append(station->date(j), station->data(j));
    }
  }

  this->m_chartView->m_chart->addSeries(series1);

  this->m_chartView->clear();
  this->m_chartView->addSeries(series1, this->m_productName);

  QDateTimeAxis *axisX = new QDateTimeAxis(this);
  axisX->setTickCount(5);
  if (this->m_requestStartDate.daysTo(this->m_requestEndDate) > 90)
    axisX->setFormat("MM/yyyy");
  else if (this->m_requestStartDate.daysTo(this->m_requestEndDate) > 4)
    axisX->setFormat("MM/dd/yyyy");
  else
    axisX->setFormat("MM/dd/yyyy hh:mm");
  axisX->setTitleText("Date (" + this->m_tz->abbreviation() + ")");
  axisX->setMin(minDateTime);
  axisX->setMax(maxDateTime);
  this->m_chartView->m_chart->addAxis(axisX, Qt::AlignBottom);
  series1->attachAxis(axisX);

  QValueAxis *axisY = new QValueAxis(this);
  axisY->setLabelFormat(format);
  axisY->setTitleText(this->m_productName.split(",").value(0));
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
  this->m_chartView->m_chart->legend()->markers().at(0)->setFont(
      QFont("Helvetica", 10, QFont::Bold));

  this->m_chartView->m_chart->setTitle(tr("USGS Station ") +
                                       this->m_currentStation.id() + ": " +
                                       this->m_currentStation.name());
  this->m_chartView->m_chart->setTitleFont(QFont("Helvetica", 14, QFont::Bold));
  m_chartView->setRenderHint(QPainter::Antialiasing);
  m_chartView->setChart(this->m_chartView->m_chart);

  foreach (QLegendMarker *marker,
           this->m_chartView->m_chart->legend()->markers()) {
    // Disconnect possible existing connection to avoid multiple connections
    QObject::disconnect(marker, SIGNAL(clicked()), this->m_chartView,
                        SLOT(handleLegendMarkerClicked()));
    QObject::connect(marker, SIGNAL(clicked()), this->m_chartView,
                     SLOT(handleLegendMarkerClicked()));
  }

  this->m_chartView->m_style = 1;
  this->m_chartView->m_coord =
      new QGraphicsSimpleTextItem(this->m_chartView->m_chart);
  this->m_chartView->m_coord->setPos(
      this->m_chartView->size().width() / 2 - 100,
      this->m_chartView->size().height() - 20);
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);

  this->setUSGSBeenPlotted(true);

  return 0;
}

int Usgs::saveUSGSImage(QString filename, QString filter) {
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
    // this->map->render(&imagePainter, QPoint(0, 0));
    this->m_chartView->render(&imagePainter, chartRect);

    outputFile.open(QIODevice::WriteOnly);
    pixmap.save(&outputFile, "JPG", 100);
  }

  return 0;
}

int Usgs::saveUSGSData(QString filename, QString format) {
  Hmdf *usgsOut = new Hmdf(this);
  usgsOut->addStation(this->m_allStationData->station(this->m_productIndex));

  int ierr = usgsOut->write(filename);
  if (ierr != 0) {
    emit usgsError("Error writing USGS data to file.");
  }

  delete usgsOut;

  return ierr;
}

int Usgs::setUSGSBeenPlotted(bool input) {
  this->m_usgsBeenPlotted = input;
  return 0;
}

QString Usgs::getClickedUSGSStation() { return *(this->m_selectedStation); }

QString Usgs::getLoadedUSGSStation() { return this->m_currentStation.id(); }

bool Usgs::getUSGSBeenPlotted() { return this->m_usgsBeenPlotted; }

QString Usgs::getUSGSErrorString() { return this->m_errorString; }

int Usgs::replotChart(Timezone *newTimezone) {
  int offset = newTimezone->utcOffset() * 1000;
  int totalOffset = -this->m_priorOffsetSeconds + offset;

  QVector<QLineSeries *> series;
  series.resize(this->m_chartView->m_chart->series().length());

  for (int i = 0; i < this->m_chartView->m_chart->series().length(); i++) {
    series[i] =
        static_cast<QLineSeries *>(this->m_chartView->m_chart->series().at(i));
  }
  for (int i = 0; i < series.length(); i++) {
    QList<QPointF> data = series[i]->points();
    series[i]->clear();
    for (int j = 0; j < data.length(); j++) {
      data[j].setX(data[j].x() + totalOffset);
    }
    series[i]->append(data);
  }

  QDateTime minDateTime = QDateTime::fromMSecsSinceEpoch(
      this->m_allStationData->station(0)->date(0));
  QDateTime maxDateTime =
      QDateTime::fromMSecsSinceEpoch(this->m_allStationData->station(0)->date(
          this->m_allStationData->station(0)->numSnaps()));
  minDateTime.setTimeSpec(Qt::UTC);
  maxDateTime.setTimeSpec(Qt::UTC);

  minDateTime =
      QDateTime(minDateTime.date(), QTime(minDateTime.time().hour(), 0, 0));
  maxDateTime =
      QDateTime(maxDateTime.date(), QTime(maxDateTime.time().hour() + 1, 0, 0));

  minDateTime = minDateTime.addMSecs(totalOffset);
  maxDateTime = maxDateTime.addMSecs(totalOffset);

  this->m_chartView->m_chart->axisX()->setTitleText(
      "Date (" + newTimezone->abbreviation() + ")");
  this->m_chartView->m_chart->axisX()->setMin(minDateTime);
  this->m_chartView->m_chart->axisX()->setMax(maxDateTime);

  this->m_priorOffsetSeconds = offset;

  this->m_chartView->rebuild();

  this->m_chartView->update();

  this->m_chartView->m_chart->zoomReset();

  return 0;
}
