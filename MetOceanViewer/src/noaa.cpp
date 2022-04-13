/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#include "noaa.h"
#include <QFileInfo>
#include <QGeoRectangle>
#include <QGeoShape>
#include <limits>
#include "chartview.h"
#include "generic.h"
#include "hmdf.h"
#include "noaacoops.h"

Noaa::Noaa(QQuickWidget *inMap, ChartView *inChart,
           QDateTimeEdit *inStartDateEdit, QDateTimeEdit *inEndDateEdit,
           QComboBox *inNoaaProduct, QComboBox *inNoaaUnits,
           QComboBox *inNoaaDatum, QCheckBox *inNoaaVDatum,
           QStatusBar *inStatusBar, QComboBox *inNoaaTimezoneLocation,
           QComboBox *inNoaaTimezone, StationModel *inStationModel,
           QString *inSelectedStation, QObject *parent)
    : QObject(parent) {
  this->m_quickMap = inMap;
  this->m_chartView = inChart;
  this->m_startDateEdit = inStartDateEdit;
  this->m_endDateEdit = inEndDateEdit;
  this->m_comboProduct = inNoaaProduct;
  this->m_comboDatum = inNoaaDatum;
  this->m_comboUnits = inNoaaUnits;
  this->m_statusBar = inStatusBar;
  this->m_comboTimezoneLocation = inNoaaTimezoneLocation;
  this->m_comboTimezone = inNoaaTimezone;
  this->m_stationModel = inStationModel;
  this->m_productIndex = 0;
  this->m_checkNoaaVdatum = inNoaaVDatum;
  this->m_selectedStation = inSelectedStation;

  //...Initialize the station object
  this->m_currentStationData.resize(2);
  this->m_currentStationData[0] = new Hmdf(this);
  this->m_currentStationData[1] = new Hmdf(this);

  //...Initialize the timezone
  this->tz = new Timezone(this);
  this->tz->fromAbbreviation(
      this->m_comboTimezone->currentText(),
      static_cast<TZData::Location>(
          this->m_comboTimezoneLocation->currentIndex()));
  this->m_offsetSeconds = this->tz->utcOffset() * 1000;
  this->m_priorOffsetSeconds = this->m_offsetSeconds;
}

Noaa::~Noaa() {}

int Noaa::fetchNOAAData() {
  QString product1, product2;

  // Check for valid date range
  if (this->m_startDate==this->m_endDate || this->m_endDate<this->m_startDate) return MetOceanViewer::Error::NOAA_INVALIDDATERANGE;

  // Begin organizing the dates for download
  QDateTime localStartDate = this->m_startDate.addMSecs(-this->m_offsetSeconds);
  QDateTime localEndDate = this->m_endDate.addMSecs(-this->m_offsetSeconds);

  int ierr = this->getNoaaProductId(product1, product2);
  if (ierr != 0) return ierr;

  this->m_datum = this->getDatumLabel();

  NoaaCoOps *coops = new NoaaCoOps(
      this->m_station, localStartDate, localEndDate, product1, this->m_datum,
      this->m_checkNoaaVdatum->isChecked(), this->m_units, this);
  ierr = coops->get(this->m_currentStationData[0]);
  if (ierr != 0) {
    this->m_errorString = coops->errorString();
    emit noaaError(this->m_errorString);
    delete coops;
    return ierr;
  }

  delete coops;
  this->m_currentStationData[0]->setNull(false);

  if (this->m_productIndex == 0) {
    NoaaCoOps *coops = new NoaaCoOps(
        this->m_station, localStartDate, localEndDate, product2, this->m_datum,
        this->m_checkNoaaVdatum->isChecked(), this->m_units, this);
    ierr = coops->get(this->m_currentStationData[1]);
    if (ierr != 0) {
      this->m_errorString = coops->errorString();
      emit noaaError(this->m_errorString);
      delete coops;
      return ierr;
    }

    delete coops;
    this->m_currentStationData[1]->setNull(false);
  }

  this->m_loadedStationId = this->m_station.id().toInt();

  return 0;
}

int Noaa::getDataBounds(double &ymin, double &ymax) {
  ymax = -std::numeric_limits<double>::max();
  ymin = std::numeric_limits<double>::max();

  for (size_t i = 0; i < this->m_currentStationData.length(); i++) {
    if (!this->m_currentStationData[i]->null()) {
      QVector<double> data =
          this->m_currentStationData[i]->station(0)->allData();
      double min = *std::min_element(data.begin(), data.end());
      double max = *std::max_element(data.begin(), data.end());
      ymin = std::min(ymin, min);
      ymax = std::max(ymax, max);
    }
  }
  return 0;
}

QString Noaa::getUnitsLabel() {
  static QStringList unitsMetric = QStringList() << "m"
                                                 << "m"
                                                 << "m"
                                                 << "m"
                                                 << "C"
                                                 << "C"
                                                 << "m/s"
                                                 << "%"
                                                 << "mb"
                                                 << "deg"
                                                 << "m/s";

  static QStringList unitsImperial = QStringList() << "ft"
                                                   << "ft"
                                                   << "ft"
                                                   << "ft"
                                                   << "F"
                                                   << "F"
                                                   << "knot"
                                                   << "%"
                                                   << "mb"
                                                   << "deg"
                                                   << "knot";
  if (this->m_comboUnits->currentIndex() == 0) {
    return unitsMetric.at(this->m_productIndex);
  } else {
    return unitsImperial.at(this->m_productIndex);
  }
}

QString Noaa::getDatumLabel() {
  if (this->m_productIndex > 3)
    return "Stnd";
  else
    return this->m_comboDatum->currentText();
}

int Noaa::generateLabels() {
  if (this->m_productIndex == 0) {
    this->m_ylabel = tr("Water Level (") + this->getUnitsLabel() + ", " +
                     this->getDatumLabel() + ")";
  } else {
    QString product;
    this->getNoaaProductLabel(product);
    if (this->getDatumLabel() == QString()) {
      this->m_ylabel = product + " (" + this->getUnitsLabel() + ")";
    } else {
      this->m_ylabel = product + " (" + this->getUnitsLabel() + ", " +
                       this->getDatumLabel() + ")";
    }
  }
  this->m_plotTitle = tr("Station ") + this->m_station.id() + ": " +
                      this->m_currentStationData[0]->station(0)->name();
  return 0;
}

QString Noaa::getNOAAErrorString() { return this->m_errorString; }

int Noaa::getLoadedNOAAStation() { return this->m_loadedStationId; }

int Noaa::getClickedNOAAStation() { return this->m_station.id().toInt(); }

int Noaa::plotChart() {
  double ymin, ymax;
  QString S1, S2;

  int offset = Timezone::localMachineOffsetFromUtc() * 1000;

  this->m_chartView->clear();

  this->m_chartView->initializeAxis(1);

  //...Create the line series
  this->generateLabels();
  this->getNoaaProductSeriesNaming(S1, S2);
  this->getDataBounds(ymin, ymax);

  this->m_currentStationData[0]->setUnits(this->m_units);
  this->m_currentStationData[0]->setDatum(this->m_datum);
  this->m_currentStationData[1]->setUnits(this->m_units);
  this->m_currentStationData[1]->setDatum(this->m_datum);

  QLineSeries *series1 = new QLineSeries(this->m_chartView->chart());
  QLineSeries *series2 = new QLineSeries(this->m_chartView->chart());
  series1->setName(S1);
  series2->setName(S2);
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  series2->setPen(
      QPen(QColor(0, 255, 0), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  QDateTime minDateTime = this->m_startDateEdit->dateTime();
  QDateTime maxDateTime = this->m_endDateEdit->dateTime();

  this->m_chartView->dateAxis()->setTitleText("Date (" +
                                              this->tz->abbreviation() + ")");

  this->m_chartView->yAxis()->setTitleText(this->m_ylabel);
  this->m_chartView->setDateFormat(minDateTime, maxDateTime);
  this->m_chartView->setAxisLimits(minDateTime, maxDateTime, ymin, ymax);

  for (size_t j = 0; j < this->m_currentStationData[0]->station(0)->numSnaps();
       j++) {
    if (QDateTime::fromMSecsSinceEpoch(
            this->m_currentStationData[0]->station(0)->date(j) +
                this->m_offsetSeconds,
            Qt::UTC)
            .isValid()) {
      if (this->m_currentStationData[0]->station(0)->data(j) != 0.0)
        series1->append(this->m_currentStationData[0]->station(0)->date(j) +
                            this->m_offsetSeconds - offset,
                        this->m_currentStationData[0]->station(0)->data(j));
    }
  }

  this->m_chartView->addSeries(series1, series1->name());

  if (this->m_productIndex == 0) {
    for (size_t j = 0;
         j < this->m_currentStationData[1]->station(0)->numSnaps(); j++)
      if (QDateTime::fromMSecsSinceEpoch(
              this->m_currentStationData[1]->station(0)->date(j) +
                  this->m_offsetSeconds,
              Qt::UTC)
              .isValid()) {
        if (this->m_currentStationData[1]->station(0)->data(j) != 0.0)
          series2->append(this->m_currentStationData[1]->station(0)->date(j) +
                              this->m_offsetSeconds - offset,
                          this->m_currentStationData[1]->station(0)->data(j));
      }
    this->m_chartView->addSeries(series2, series2->name());
  }

  this->m_chartView->chart()->setTitle(tr("NOAA Station ") +
                                       this->m_station.id() + ": " +
                                       this->m_station.name());

  this->m_chartView->initializeLegendMarkers();
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);

  return 0;
}

int Noaa::plotNOAAStation() {
  if (*(this->m_selectedStation) == "-1") {
    emit noaaError(tr("You must select a station"));
    return 1;
  } else {
    this->m_station =
        this->m_stationModel->findStation(*(this->m_selectedStation));
    //this->m_station.id() = this->m_station.id().toInt();

    //...Grab the options from the UI
    this->m_startDate = this->m_startDateEdit->dateTime();
    this->m_endDate = this->m_endDateEdit->dateTime();
    this->m_units = this->m_comboUnits->currentText();
    this->m_datum = this->m_comboDatum->currentText();
    this->m_productIndex = this->m_comboProduct->currentIndex();

    // Update status
    this->m_statusBar->showMessage(tr("Downloading data from NOAA...", 0));

    int ierr = this->fetchNOAAData();
    if (ierr != MetOceanViewer::Error::NOERR) {
      this->m_statusBar->clearMessage();
      return ierr;
    }

    //...Update the status bar
    this->m_statusBar->showMessage(tr("Plotting the data from NOAA..."));

    //...Check for valid data
    if (this->m_currentStationData[0]->station(0)->numSnaps() < 5) {
      emit noaaError(this->m_errorString);
      this->m_statusBar->clearMessage();
      return 1;
    }

    //...Plot the chart
    ierr = this->plotChart();
    if (ierr != MetOceanViewer::Error::NOERR) return ierr;

    this->m_statusBar->clearMessage();

    return 0;
  }
}

int Noaa::getNoaaProductId(QString &product1, QString &product2) {
  static QStringList noaaProductCode = QStringList() << "water_level"
                                                     << "water_level"
                                                     << "hourly_height"
                                                     << "predictions"
                                                     << "air_temperature"
                                                     << "water_temperature"
                                                     << "wind:speed"
                                                     << "humidity"
                                                     << "air_pressure"
                                                     << "wind:direction"
                                                     << "wind:gusts";

  product1 = noaaProductCode.at(this->m_productIndex);
  if (this->m_productIndex == 0)
    product2 = "predictions";
  else
    product2 = QString();
  return 0;
}

int Noaa::getNoaaProductLabel(QString &product) {
  static QStringList productString =
      QStringList() << "6 Min Observed Water Level vs. Predicted"
                    << "6 Min Observed Water Level"
                    << "Hourly Observed Water Level"
                    << "Predicted Water Level"
                    << "Air Temperature"
                    << "Water Temperature"
                    << "Wind Speed"
                    << "Relative Humidity"
                    << "Air Pressure"
                    << "Wind Direction"
                    << "Wind Gusts";
  product = productString.at(this->m_productIndex);
  return 0;
}

int Noaa::getNoaaProductSeriesNaming(QString &product1, QString &product2) {
  static QStringList plotFileCode = QStringList()
                                    << "Observed"
                                    << "6 min Observed Water Level"
                                    << "Hourly Water Level"
                                    << "Predicted Water Level"
                                    << "Air Temperature"
                                    << "Water Temperature"
                                    << "Wind Speed"
                                    << "Humidity"
                                    << "Air Pressure"
                                    << "Wind Direction"
                                    << "Wind Gusts";
  product1 = plotFileCode.at(this->m_productIndex);
  if (this->m_productIndex == 0) {
    product2 = "Predicted";
  } else {
    product2 = QString();
  }
  return 0;
}

int Noaa::saveNOAAImage(QString filename, QString filter) {
  if (filter == "PDF (*.pdf)") {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize::Letter);
    printer.setResolution(400);
    printer.setPageOrientation(QPageLayout::Landscape);
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

int Noaa::saveNOAAData(QString filename) {
  if (!this->m_currentStationData[0]->null() &&
      !this->m_currentStationData[1]->null()) {
    QFileInfo fn(filename);
    QString directory = fn.absoluteDir().absolutePath();
    QString file = fn.fileName();
    QString obs = directory + "/Observations_" + file;
    QString pre = directory + "/Predictions_" + file;

    int ierr = this->m_currentStationData[0]->write(obs);
    if (ierr != 0)
      emit noaaError("Error writing NOAA observation data to file");

    ierr = this->m_currentStationData[1]->write(pre);
    if (ierr != 0) emit noaaError("Error writing NOAA prediction data to file");
  } else {
    int ierr = this->m_currentStationData[0]->write(filename);
    if (ierr != 0) emit noaaError("Error writing NOAA data to file");
  }

  return 0;
}

int Noaa::replotChart(Timezone *newTimezone) {
  int offset = newTimezone->utcOffset() * 1000;
  int totalOffset = -this->m_priorOffsetSeconds + offset;

  QVector<QLineSeries *> series;
  series.resize(this->m_chartView->chart()->series().length());

  for (size_t i = 0; i < this->m_chartView->chart()->series().length(); i++) {
    series[i] =
        static_cast<QLineSeries *>(this->m_chartView->chart()->series().at(i));
  }

  for (size_t i = 0; i < series.length(); i++) {
    QList<QPointF> data = series[i]->points();
    series[i]->clear();
    for (size_t j = 0; j < data.length(); j++) {
      data[j].setX(data[j].x() + totalOffset);
    }
    series[i]->append(data);
  }

  QDateTime minDateTime = this->m_startDateEdit->dateTime();
  QDateTime maxDateTime = this->m_endDateEdit->dateTime();
  minDateTime.setTimeSpec(Qt::UTC);
  maxDateTime.setTimeSpec(Qt::UTC);

  minDateTime = minDateTime.addMSecs(totalOffset);
  maxDateTime = maxDateTime.addMSecs(totalOffset);

  this->m_chartView->dateAxis()->setTitleText(
      "Date (" + newTimezone->abbreviation() + ")");
  this->m_chartView->dateAxis()->setMin(minDateTime);
  this->m_chartView->dateAxis()->setMax(maxDateTime);

  this->m_priorOffsetSeconds = offset;

  this->m_chartView->rebuild();

  this->m_chartView->update();

  this->m_chartView->chart()->zoomReset();

  return 0;
}
