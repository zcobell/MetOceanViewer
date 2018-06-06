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
#include "usertimeseries.h"
#include "adcircstationoutput.h"
#include "dflow.h"
#include "errors.h"
#include "filetypes.h"
#include "generic.h"
#include "metoceanviewer.h"
#include "netcdf.h"
#include "proj4.h"

UserTimeseries::UserTimeseries(
    QTableWidget *inTable, QCheckBox *inXAxisCheck, QCheckBox *inYAxisCheck,
    QDateEdit *inStartDate, QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
    QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle, QLineEdit *inXLabelEdit,
    QLineEdit *inYLabelEdit, QQuickWidget *inMap, ChartView *inChart,
    QStatusBar *inStatusBar, QVector<QColor> inRandomColorList,
    StationModel *inStationModel, QString *inSelectedStation, QObject *parent)
    : QObject(parent) {
  this->m_table = inTable;
  this->m_checkXaxis = inXAxisCheck;
  this->m_checkYaxis = inYAxisCheck;
  this->m_startDateEdit = inStartDate;
  this->m_endDateEdit = inEndDate;
  this->m_yMaxEdit = inYMaxEdit;
  this->m_yMinEdit = inYMinEdit;
  this->m_plotTitle = inPlotTitle;
  this->m_xLabelEdit = inXLabelEdit;
  this->m_yLabelEdit = inYLabelEdit;
  this->m_quickMap = inMap;
  this->m_chartView = inChart;
  this->m_statusBar = inStatusBar;
  this->m_randomColorList = inRandomColorList;
  this->m_markerId = 0;
  this->m_chartView->m_chart = nullptr;
  this->m_stationmodel = inStationModel;
  this->m_currentStation = inSelectedStation;
}

UserTimeseries::~UserTimeseries() {}

int UserTimeseries::getDataBounds(double &ymin, double &ymax,
                                  QDateTime &minDateOut, QDateTime &maxDateOut,
                                  QVector<double> timeAddList) {
  qint64 nullDate = 0;

  ymin = DBL_MAX;
  ymax = DBL_MIN;
  qint64 minDate =
      QDateTime(QDate(3000, 1, 1), QTime(0, 0, 0)).toMSecsSinceEpoch();
  qint64 maxDate =
      QDateTime(QDate(1500, 1, 1), QTime(0, 0, 0)).toMSecsSinceEpoch();

  for (int i = 0; i < this->m_fileDataUnique.length(); i++) {
    double unitConversion = this->m_table->item(i, 3)->text().toDouble();
    double addY = this->m_table->item(i, 5)->text().toDouble();
    for (int k = 0; k < this->m_selectedStations.length(); k++) {
      if (!this->m_fileDataUnique[i]
               ->station(this->m_selectedStations[k])
               ->isNull()) {
        for (int j = 0; j < this->m_fileDataUnique[i]
                                ->station(this->m_selectedStations[k])
                                ->numSnaps();
             j++) {
          if (this->m_fileDataUnique[i]
                              ->station(this->m_selectedStations[k])
                              ->data(j) *
                          unitConversion +
                      addY <
                  ymin &&
              this->m_fileDataUnique[i]
                      ->station(this->m_selectedStations[k])
                      ->data(j) != MetOceanViewer::NULL_TS)
            ymin = this->m_fileDataUnique[i]
                           ->station(this->m_selectedStations[k])
                           ->data(j) *
                       unitConversion +
                   addY;
          if (this->m_fileDataUnique[i]
                              ->station(this->m_selectedStations[k])
                              ->data(j) *
                          unitConversion +
                      addY >
                  ymax &&
              this->m_fileDataUnique[i]
                      ->station(this->m_selectedStations[k])
                      ->data(j) != MetOceanViewer::NULL_TS)
            ymax = this->m_fileDataUnique[i]
                           ->station(this->m_selectedStations[k])
                           ->data(j) *
                       unitConversion +
                   addY;
          if (this->m_fileDataUnique[i]
                          ->station(this->m_selectedStations[k])
                          ->date(j) +
                      (timeAddList[i] * 3600.0) <
                  minDate &&
              this->m_fileDataUnique[i]
                      ->station(this->m_selectedStations[k])
                      ->date(j) != nullDate)
            minDate = this->m_fileDataUnique[i]
                          ->station(this->m_selectedStations[k])
                          ->date(j) +
                      (timeAddList[i] * 3600.0);
          if (this->m_fileDataUnique[i]
                          ->station(this->m_selectedStations[k])
                          ->date(j) +
                      (timeAddList[i] * 3600.0) >
                  maxDate &&
              this->m_fileDataUnique[i]
                      ->station(this->m_selectedStations[k])
                      ->date(j) != nullDate)
            maxDate = this->m_fileDataUnique[i]
                          ->station(this->m_selectedStations[k])
                          ->date(j) +
                      (timeAddList[i] * 3600.0);
        }
      }
    }
  }

  minDateOut = QDateTime::fromMSecsSinceEpoch(minDate);
  maxDateOut = QDateTime::fromMSecsSinceEpoch(maxDate);

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::saveImage(QString filename, QString filter) {
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

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::getCurrentMarkerID() { return this->m_markerId; }

int UserTimeseries::getClickedMarkerID() {
  return this->m_currentStation->split(",").value(0).toInt();
}

int UserTimeseries::getStationSelections() {
  if (this->m_currentStation == QString()) {
    emit timeseriesError(tr("No stations selected!"));
    return 1;
  }

  QString tempString;
  QStringList dataList = this->m_currentStation->split(",");
  tempString = dataList.value(0);
  int nMarkers = dataList.length();

  if (nMarkers > 0) {
    this->m_selectedStations.resize(nMarkers);
    for (int i = 0; i < nMarkers; i++) {
      tempString = dataList.value(i);
      this->m_selectedStations[i] = tempString.toInt();
    }
  } else {
    emit timeseriesError(tr("No stations selected"));
    return 1;
  }
  return MetOceanViewer::Error::NOERR;
}

void UserTimeseries::plot() {
  int i, j, k, ierr, colorCounter;
  qint64 TempDate;
  qreal TempValue;
  double unitConversion, addX, addY;
  QVector<double> addXList;
  double ymin, ymax;
  QVector<QLineSeries *> series;
  QColor seriesColor;
  QDateTime minDate, maxDate;

  colorCounter = -1;

  //...At some point this may no longer be
  //   needed, but this defines an offset from UTC
  //   For some reason, the qDatTime axis operate in local
  //   time, which can show an offset when converting to mSecSinceEpoch
  QDateTime now = QDateTime::currentDateTime();
  qint64 offset = now.offsetFromUtc() * qint64(1000);
  qint64 endDate = this->m_endDateEdit->dateTime().toMSecsSinceEpoch();
  qint64 startDate = this->m_startDateEdit->dateTime().toMSecsSinceEpoch();

  ierr = this->getStationSelections();
  if (ierr != MetOceanViewer::Error::NOERR) return;

  addXList.resize(this->m_fileDataUnique.length());
  for (i = 0; i < this->m_fileDataUnique.length(); i++)
    addXList[i] = this->m_table->item(i, 4)->text().toDouble();

  this->m_markerId = this->m_selectedStations[0];
  ierr = this->getDataBounds(ymin, ymax, minDate, maxDate, addXList);

  QDateTimeAxis *axisX;
  QValueAxis *axisY;

  if (this->m_chartView->m_chart == nullptr) {
    this->m_chartView->m_chart = new QChart();
    axisX = new QDateTimeAxis(this->m_chartView->m_chart);
    axisY = new QValueAxis(this->m_chartView->m_chart);
    this->m_chartView->m_coord =
        new QGraphicsSimpleTextItem(this->m_chartView->m_chart);
    this->m_chartView->m_chart->addAxis(axisX, Qt::AlignBottom);
    this->m_chartView->m_chart->addAxis(axisY, Qt::AlignLeft);
  } else {
    this->m_chartView->m_chart->removeAllSeries();
    axisX = static_cast<QDateTimeAxis *>(this->m_chartView->m_chart->axisX());
    axisY = static_cast<QValueAxis *>(this->m_chartView->m_chart->axisY());
  }

  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);

  axisX->setTickCount(5);
  axisX->setTitleText("Date");
  if (!this->m_checkXaxis->isChecked()) {
    axisX->setMin(this->m_startDateEdit->dateTime());
    axisX->setMax(this->m_endDateEdit->dateTime());
  } else {
    minDate = minDate.addMSecs(-offset);
    maxDate = maxDate.addMSecs(-offset);
    axisX->setMin(minDate);
    axisX->setMax(maxDate);
  }

  axisX->setTitleFont(QFont("Helvetica", 10, QFont::Bold));

  axisY->setTickCount(5);
  axisY->setTitleText(this->m_yLabelEdit->text());
  if (!this->m_checkYaxis->isChecked()) {
    axisY->setMin(this->m_yMinEdit->value());
    axisY->setMax(this->m_yMaxEdit->value());
  } else {
    axisY->setMin(ymin);
    axisY->setMax(ymax);
  }
  axisY->setTitleFont(QFont("Helvetica", 10, QFont::Bold));

  if (axisX->min().daysTo(axisX->max()) > 90)
    axisX->setFormat("MM/yyyy");
  else if (axisX->min().daysTo(axisX->max()) > 4)
    axisX->setFormat("MM/dd/yyyy");
  else
    axisX->setFormat("MM/dd/yyyy hh:mm");

  int seriesCounter = 0;
  int plottedSeriesCounter = 0;

  this->m_chartView->clear();

  for (i = 0; i < this->m_fileDataUnique.length(); i++) {
    if (this->m_selectedStations.length() == 1) {
      seriesCounter = seriesCounter + 1;
      series.resize(seriesCounter);
      series[seriesCounter - 1] = new QLineSeries(this->m_chartView->m_chart);
      series[seriesCounter - 1]->setName(
          this->m_table->item(seriesCounter - 1, 1)->text());
      seriesColor.setNamedColor(
          this->m_table->item(seriesCounter - 1, 2)->text());
      series[seriesCounter - 1]->setPen(
          QPen(seriesColor, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      unitConversion =
          this->m_table->item(seriesCounter - 1, 3)->text().toDouble();
      addX =
          this->m_table->item(seriesCounter - 1, 4)->text().toDouble() * 3.6e+6;
      addY = this->m_table->item(seriesCounter - 1, 5)->text().toDouble();
      for (j = 0;
           j < this->m_fileDataUnique[i]->station(this->m_markerId)->numSnaps();
           j++) {
        if (this->m_fileDataUnique[i]->station(this->m_markerId)->data(j) !=
                MetOceanViewer::NULL_TS &&
            this->m_fileDataUnique[i]->station(this->m_markerId)->date(j) >=
                startDate &&
            this->m_fileDataUnique[i]->station(this->m_markerId)->date(j) <=
                endDate) {
          TempDate =
              this->m_fileDataUnique[i]->station(this->m_markerId)->date(j) +
              addX - offset;
          TempValue =
              this->m_fileDataUnique[i]->station(this->m_markerId)->data(j) *
                  unitConversion +
              addY;
          series[seriesCounter - 1]->append(TempDate, TempValue);
        }
      }

      if (series[seriesCounter - 1]->points().size() > 0) {
        plottedSeriesCounter = plottedSeriesCounter + 1;
        this->m_chartView->m_chart->addSeries(series[seriesCounter - 1]);
        this->m_chartView->m_chart->legend()
            ->markers()
            .at(plottedSeriesCounter - 1)
            ->setFont(QFont("Helvetica", 10, QFont::Bold));
        series[seriesCounter - 1]->attachAxis(axisX);
        series[seriesCounter - 1]->attachAxis(axisY);
        this->m_chartView->addSeries(series[seriesCounter - 1],
                                     series[seriesCounter - 1]->name());
      }
    } else {
      //...Plot multiple stations. We use random colors and append the station
      // number
      for (k = 0; k < this->m_selectedStations.length(); k++) {
        if (!this->m_fileDataUnique[i]
                 ->station(this->m_selectedStations[k])
                 ->isNull()) {
          seriesCounter = seriesCounter + 1;
          colorCounter = colorCounter + 1;

          //...Loop the colors
          if (colorCounter >= this->m_randomColorList.length())
            colorCounter = 0;

          series.resize(seriesCounter);
          series[seriesCounter - 1] = new QLineSeries(this->m_chartView);
          series[seriesCounter - 1]->setName(
              this->m_fileDataUnique[i]
                  ->station(this->m_selectedStations[k])
                  ->name() +
              QStringLiteral(": ") + this->m_table->item(i, 1)->text());
          seriesColor = this->m_randomColorList[colorCounter];
          series[seriesCounter - 1]->setPen(
              QPen(seriesColor, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
          unitConversion = this->m_table->item(i, 3)->text().toDouble();
          addX = this->m_table->item(i, 4)->text().toDouble() * 3.6e+6;
          addY = this->m_table->item(i, 5)->text().toDouble();
          for (j = 0; j < this->m_fileDataUnique[i]
                              ->station(this->m_selectedStations[k])
                              ->numSnaps();
               j++) {
            if (this->m_fileDataUnique[i]
                        ->station(this->m_selectedStations[k])
                        ->data(j) != MetOceanViewer::NULL_TS &&
                this->m_fileDataUnique[i]
                        ->station(this->m_selectedStations[k])
                        ->date(j) >= startDate &&
                this->m_fileDataUnique[i]
                        ->station(this->m_selectedStations[k])
                        ->date(j) <= endDate) {
              TempDate = this->m_fileDataUnique[i]
                             ->station(this->m_selectedStations[k])
                             ->date(j) +
                         addX - offset;
              TempValue = this->m_fileDataUnique[i]
                                  ->station(this->m_selectedStations[k])
                                  ->data(j) *
                              unitConversion +
                          addY;
              series[seriesCounter - 1]->append(TempDate, TempValue);
            }
          }

          if (series[seriesCounter - 1]->points().size() > 0) {
            this->m_chartView->m_chart->addSeries(series[seriesCounter - 1]);
            this->m_chartView->m_chart->legend()
                ->markers()
                .at(seriesCounter - 1)
                ->setFont(QFont("Helvetica", 10, QFont::Bold));
            series[seriesCounter - 1]->attachAxis(axisX);
            series[seriesCounter - 1]->attachAxis(axisY);
            this->m_chartView->addSeries(series[seriesCounter - 1],
                                         series[seriesCounter - 1]->name());
          }
        }
      }
    }
  }

  axisY->setTickCount(10);
  axisY->applyNiceNumbers();
  axisX->setGridLineColor(QColor(200, 200, 200));
  axisY->setGridLineColor(QColor(200, 200, 200));
  axisY->setShadesPen(Qt::NoPen);
  axisY->setShadesBrush(QBrush(QColor(240, 240, 240)));
  axisY->setShadesVisible(true);
  axisY->applyNiceNumbers();

  if (this->m_selectedStations.length() == 1)
    this->m_chartView->m_chart->setTitle(
        this->m_plotTitle->text() + ": " +
        this->m_fileDataUnique[0]->station(this->m_markerId)->name());
  else
    this->m_chartView->m_chart->setTitle(this->m_plotTitle->text());

  this->m_chartView->m_chart->setTitleFont(QFont("Helvetica", 14, QFont::Bold));
  this->m_chartView->setRenderHint(QPainter::Antialiasing);
  this->m_chartView->setChart(this->m_chartView->m_chart);

  foreach (QLegendMarker *marker,
           this->m_chartView->m_chart->legend()->markers()) {
    // Disconnect possible existing connection to avoid multiple connections
    disconnect(marker, SIGNAL(clicked()), this->m_chartView,
               SLOT(handleLegendMarkerClicked()));
    connect(marker, SIGNAL(clicked()), this->m_chartView,
            SLOT(handleLegendMarkerClicked()));
  }

  this->m_chartView->m_style = 1;
  this->m_chartView->m_coord->setPos(
      this->m_chartView->size().width() / 2 - 100,
      this->m_chartView->size().height() - 20);
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);

  return;
}

QString UserTimeseries::getErrorString() { return this->m_errorString; }

int UserTimeseries::processImedsData(int tableIndex, Hmdf *data) {
  QString tempFile = this->m_table->item(tableIndex, 6)->text();

  int ierr = data->readImeds(tempFile);

  if (ierr != MetOceanViewer::Error::NOERR) {
    this->m_errorString = tr("Error reading file: ") + tempFile;
    return MetOceanViewer::Error::IMEDS_FILEREADERROR;
  }

  data->setSuccess(true);

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processAdcircNetcdfData(int tableIndex, Hmdf *data) {
  QString tempFile = this->m_table->item(tableIndex, 6)->text();
  QDateTime coldStart = QDateTime::fromString(
      this->m_table->item(tableIndex, 7)->text(), "yyyy-MM-dd hh:mm:ss");
  AdcircStationOutput *adcircData = new AdcircStationOutput(this);
  int ierr = adcircData->read(tempFile, coldStart);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->m_errorString = tr("Error reading file: ") + tempFile;
    return MetOceanViewer::Error::ADCIRC_NETCDFREADERROR;
  }

  ierr = adcircData->toHmdf(data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    delete adcircData;
    return ierr;
  }
  delete adcircData;

  if (!data->success()) return MetOceanViewer::Error::ADCIRC_NETCDFTOIMEDS;
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processAdcircAsciiData(int tableIndex, Hmdf *data) {
  QString tempFile = this->m_table->item(tableIndex, 6)->text();
  QDateTime coldStart = QDateTime::fromString(
      this->m_table->item(tableIndex, 7)->text(), "yyyy-MM-dd hh:mm:ss");
  QString tempStationFile = this->m_table->item(tableIndex, 10)->text();
  AdcircStationOutput *adcircData = new AdcircStationOutput(this);

  int ierr = adcircData->read(tempFile, tempStationFile, coldStart);

  if (ierr != MetOceanViewer::Error::NOERR) {
    this->m_errorString = tr("Error reading file: ") + tempFile;
    return MetOceanViewer::Error::ADCIRC_ASCIIREADERROR;
  }

  ierr = adcircData->toHmdf(data);
  delete adcircData;
  if (ierr != MetOceanViewer::Error::NOERR) {
    return ierr;
  }

  if (!data->success()) return MetOceanViewer::Error::ADCIRC_ASCIITOIMEDS;
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processDflowData(int tableIndex, Hmdf *data) {
  QString tempFile = this->m_table->item(tableIndex, 6)->text();

  Dflow *dflow = new Dflow(tempFile, this);
  QString dflowVar = this->m_table->item(tableIndex, 12)->text();
  int dflowLayer = this->m_table->item(tableIndex, 13)->text().toInt();
  int ierr = dflow->getVariable(dflowVar, dflowLayer, data);

  if (ierr != MetOceanViewer::Error::NOERR) {
    this->m_errorString =
        tr("Error processing DFlow: ") + dflow->error->toString();
    delete dflow;
    return MetOceanViewer::Error::DFLOW_FILEREADERROR;
  }
  delete dflow;
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processGenericNetcdfData(int tableIndex, Hmdf *data) {
  QString tempFile = this->m_table->item(tableIndex, 6)->text();

  int ierr = data->readNetcdf(tempFile);
  if (ierr != 0) {
    this->m_errorString = "Error processing generic netcdf file.";
    return MetOceanViewer::Error::GENERICNETCDFERROR;
  }
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processDataFiles() {
  int ierr;

  for (int i = 0; i < this->m_table->rowCount(); i++) {
    int inputFileType =
        Filetypes::getIntegerFiletype(this->m_table->item(i, 6)->text());
    this->m_epsg.push_back(this->m_table->item(i, 11)->text().toInt());

    Hmdf *stationData = new Hmdf(this);

    switch (inputFileType) {
      case MetOceanViewer::FileType::ASCII_IMEDS:
        ierr = this->processImedsData(i, stationData);
        this->m_allFileData.push_back(stationData);
        break;
      case MetOceanViewer::FileType::NETCDF_ADCIRC:
        ierr = this->processAdcircNetcdfData(i, stationData);
        this->m_allFileData.push_back(stationData);
        break;
      case MetOceanViewer::FileType::ASCII_ADCIRC:
        ierr = this->processAdcircAsciiData(i, stationData);
        this->m_allFileData.push_back(stationData);
        break;
      case MetOceanViewer::FileType::NETCDF_DFLOW:
        ierr = this->processDflowData(i, stationData);
        this->m_allFileData.push_back(stationData);
        break;
      case MetOceanViewer::FileType::NETCDF_GENERIC:
        ierr = this->processGenericNetcdfData(i, stationData);
        this->m_allFileData.push_back(stationData);
        break;
      default:
        this->m_errorString = QStringLiteral("Invalid file format");
        return MetOceanViewer::Error::INVALIDFILEFORMAT;
    }

    if (!this->m_allFileData.last()->success() ||
        ierr != MetOceanViewer::Error::NOERR) {
      this->m_allFileData.pop_back();
      return MetOceanViewer::Error::GENERICFILEREADERROR;
    }
  }
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processStationLocations() {
  //...Build a unique set of timeseries data
  if (this->m_allFileData.length() == 1) {
    for (int i = 0; i < this->m_allFileData[0]->nstations(); i++) {
      this->m_xLocations.push_back(
          this->m_allFileData[0]->station(i)->longitude());
      this->m_yLocations.push_back(
          this->m_allFileData[0]->station(i)->latitude());
      this->m_fileDataUnique = this->m_allFileData;
    }
  } else {
    int ierr = this->getUniqueStationList(
        this->m_allFileData, this->m_xLocations, this->m_yLocations);
    if (ierr != MetOceanViewer::Error::NOERR) {
      this->m_errorString = tr("Error building the station list");
      return MetOceanViewer::Error::BUILDSTATIONLIST;
    }
  }

  int ierr =
      this->buildRevisedIMEDS(this->m_allFileData, this->m_xLocations,
                              this->m_yLocations, this->m_fileDataUnique);

  if (ierr != 0) {
    this->m_errorString = tr("Error building the unique dataset");
    return MetOceanViewer::Error::BUILDREVISEDIMEDS;
  }

  this->m_allFileData.clear();

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::addMarkersToMap() {
  this->m_stationmodel->clear();

  //...Add the markers to the map
  for (int i = 0; i < this->m_fileDataUnique[0]->nstations(); i++) {
    double x = -1.0;
    double y = -1.0;
    QString StationName = "NONAME";

    // Check that we aren't sending a null location to
    // the backend
    for (int j = 0; j < this->m_fileDataUnique.length(); j++) {
      if (!this->m_fileDataUnique[j]->station(i)->isNull()) {
        StationName = this->m_fileDataUnique[j]->station(i)->name();
        x = this->m_fileDataUnique[j]->station(i)->longitude();
        y = this->m_fileDataUnique[j]->station(i)->latitude();
        break;
      }
    }

    this->m_stationmodel->addMarker(
        Station(QGeoCoordinate(y, x), QString::number(i), StationName));
  }

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::processData() {
  //...Process the data files
  int ierr = this->processDataFiles();
  if (ierr != MetOceanViewer::Error::NOERR) {
    return ierr;
  }

  //...Project the data to WGS84
  ierr = this->projectStations(this->m_epsg, this->m_allFileData);
  if (ierr != 0) {
    this->m_errorString = tr("Error projecting the station locations");
    return MetOceanViewer::Error::PROJECTSTATIONS;
  }

  //...Perform data organization
  ierr = this->processStationLocations();
  if (ierr != MetOceanViewer::Error::NOERR) {
    return ierr;
  }

  //...Add the markers
  ierr = this->addMarkersToMap();
  if (ierr != MetOceanViewer::Error::NOERR) {
    return ierr;
  }

  return MetOceanViewer::Error::NOERR;
}

//-------------------------------------------//
// Generate a unique list of stations so that
// we can later build a complete list of stations
// and not show data where it doesn't exist for
// certain files
//-------------------------------------------//
int UserTimeseries::getUniqueStationList(QVector<Hmdf *> Data,
                                         QVector<double> &X,
                                         QVector<double> &Y) {
  for (int i = 0; i < Data.length(); i++) {
    for (int j = 0; j < Data[i]->nstations(); j++) {
      bool found = false;
      for (int k = 0; k < X.length(); k++) {
        double dx = Data[i]->station(j)->longitude() - X[k];
        double dy = Data[i]->station(j)->latitude() - Y[k];
        double d = qSqrt(qPow(dx, 2.0) + qPow(dy, 2.0));
        if (d < this->m_duplicateStationTolerance) {
          found = true;
          break;
        }
      }
      if (!found) {
        X.push_back(Data[i]->station(j)->longitude());
        Y.push_back(Data[i]->station(j)->latitude());
      }
    }
  }
  return MetOceanViewer::Error::NOERR;
}
//-------------------------------------------//

//-------------------------------------------//
// Build a revised set of IMEDS data series
// which will have null data where there was
// not data in the file
//-------------------------------------------//
int UserTimeseries::buildRevisedIMEDS(QVector<Hmdf *> Data, QVector<double> X,
                                      QVector<double> Y,
                                      QVector<Hmdf *> &DataOut) {
  DataOut.resize(Data.length());

  for (int i = 0; i < Data.length(); i++) {
    DataOut[i] = new Hmdf(this);
    DataOut[i]->setHeader1(Data[i]->header1());
    DataOut[i]->setHeader2(Data[i]->header2());
    DataOut[i]->setHeader3(Data[i]->header3());

    for (int j = 0; j < X.length(); j++) {
      HmdfStation *station = new HmdfStation(DataOut[i]);
      station->setLongitude(X[j]);
      station->setLatitude(Y[j]);
      DataOut[i]->addStation(station);
    }
  }

  for (int i = 0; i < Data.length(); i++) {
    for (int j = 0; j < DataOut[i]->nstations(); j++) {
      bool found = false;
      for (int k = 0; k < Data[i]->nstations(); k++) {
        double dx = Data[i]->station(k)->longitude() -
                    DataOut[i]->station(j)->longitude();
        double dy = Data[i]->station(k)->latitude() -
                    DataOut[i]->station(j)->latitude();
        double d = qSqrt(qPow(dx, 2.0) + qPow(dy, 2.0));
        if (d < this->m_duplicateStationTolerance) {
          DataOut[i]->station(j)->setName(Data[i]->station(k)->name());
          DataOut[i]->station(j)->setData(Data[i]->station(k)->allData());
          DataOut[i]->station(j)->setDate(Data[i]->station(k)->allDate());
          DataOut[i]->station(j)->setIsNull(false);
          found = true;
          break;
        }
      }
      if (!found) {
        // Build a station with a null dataset we can find later
        DataOut[i]->station(j)->setName("NONAME");
        DataOut[i]->station(j)->setNext(MetOceanViewer::NULL_TS, 0.0);
        DataOut[i]->station(j)->setIsNull(true);
      }
    }
  }
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::projectStations(QVector<int> epsg,
                                    QVector<Hmdf *> &projectedStations) {
  int i, j, ierr;
  double x, y, x2, y2;
  bool isLatLon;
  proj4 *projection = new proj4(this);

  for (i = 0; i < projectedStations.length(); i++) {
    if (epsg[i] != 4326) {
      for (j = 0; j < projectedStations[i]->nstations(); j++) {
        x = projectedStations[i]->station(j)->longitude();
        y = projectedStations[i]->station(j)->latitude();
        ierr = projection->transform(epsg[i], 4326, x, y, x2, y2, isLatLon);
        if (ierr != 0) return MetOceanViewer::Error::PROJECTSTATIONS;
        projectedStations[i]->station(j)->setLongitude(x2);
        projectedStations[i]->station(j)->setLatitude(y2);
      }
    }
  }
  delete projection;
  return MetOceanViewer::Error::NOERR;
}
