/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
#include "javascriptasyncreturn.h"
#include "metoceanviewer.h"
#include "netcdf.h"
#include "proj4.h"
#define _DUPLICATE_STATION_TOL 0.00001

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
  int i, j, k;
  double unitConversion, addY;
  long long nullDate = 0;

  ymin = DBL_MAX;
  ymax = DBL_MIN;
  long long minDate =
      QDateTime(QDate(3000, 1, 1), QTime(0, 0, 0)).toMSecsSinceEpoch();
  long long maxDate =
      QDateTime(QDate(1500, 1, 1), QTime(0, 0, 0)).toMSecsSinceEpoch();

  for (i = 0; i < this->m_fileDataUnique.length(); i++) {
    unitConversion = this->m_table->item(i, 3)->text().toDouble();
    addY = this->m_table->item(i, 5)->text().toDouble();
    for (k = 0; k < this->m_selectedStations.length(); k++) {
      if (!this->m_fileDataUnique[i]
               ->station[this->m_selectedStations[k]]
               .isNull()) {
        for (j = 0; j < this->m_fileDataUnique[i]
                            ->station[this->m_selectedStations[k]]
                            .numSnaps();
             j++) {
          if (this->m_fileDataUnique[i]
                              ->station[this->m_selectedStations[k]]
                              .data(j) *
                          unitConversion +
                      addY <
                  ymin &&
              this->m_fileDataUnique[i]
                      ->station[this->m_selectedStations[k]]
                      .data(j) != MetOceanViewer::NULL_TS)
            ymin = this->m_fileDataUnique[i]
                           ->station[this->m_selectedStations[k]]
                           .data(j) *
                       unitConversion +
                   addY;
          if (this->m_fileDataUnique[i]
                              ->station[this->m_selectedStations[k]]
                              .data(j) *
                          unitConversion +
                      addY >
                  ymax &&
              this->m_fileDataUnique[i]
                      ->station[this->m_selectedStations[k]]
                      .data(j) != MetOceanViewer::NULL_TS)
            ymax = this->m_fileDataUnique[i]
                           ->station[this->m_selectedStations[k]]
                           .data(j) *
                       unitConversion +
                   addY;
          if (this->m_fileDataUnique[i]
                          ->station[this->m_selectedStations[k]]
                          .date(j) +
                      (timeAddList[i] * 3600.0) <
                  minDate &&
              this->m_fileDataUnique[i]
                      ->station[this->m_selectedStations[k]]
                      .date(j) != nullDate)
            minDate = this->m_fileDataUnique[i]
                          ->station[this->m_selectedStations[k]]
                          .date(j) +
                      (timeAddList[i] * 3600.0);
          if (this->m_fileDataUnique[i]
                          ->station[this->m_selectedStations[k]]
                          .date(j) +
                      (timeAddList[i] * 3600.0) >
                  maxDate &&
              this->m_fileDataUnique[i]
                      ->station[this->m_selectedStations[k]]
                      .date(j) != nullDate)
            maxDate = this->m_fileDataUnique[i]
                          ->station[this->m_selectedStations[k]]
                          .date(j) +
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

int UserTimeseries::setMarkerID() {
  this->m_markerId = this->getMarkerIDFromMap();
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::getClickedMarkerID() { return this->getMarkerIDFromMap(); }

int UserTimeseries::getMarkerIDFromMap() { return 0; }

int UserTimeseries::getMultipleMarkersFromMap() {
  QVariant eval = QVariant();
  int i;
  QString tempString;
  QString data = eval.toString();
  QStringList dataList = data.split(",");
  tempString = dataList.value(0);
  int nMarkers = tempString.toInt();

  if (nMarkers > 0) {
    this->m_selectedStations.resize(nMarkers);
    for (i = 1; i <= nMarkers; i++) {
      tempString = dataList.value(i);
      this->m_selectedStations[i - 1] = tempString.toInt();
    }
  } else
    return MetOceanViewer::Error::MARKERSELECTION;

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::getAsyncMultipleMarkersFromMap() {
  //  JavascriptAsyncReturn *javaReturn = new JavascriptAsyncReturn(this);
  //  connect(javaReturn, SIGNAL(valueChanged(QString)), this,
  //          SLOT(javascriptDataReturned(QString)));
  //  this->map->page()->runJavaScript(
  //      "getMarkers()",
  //      [javaReturn](const QVariant &v) { javaReturn->setValue(v); });
  return 0;
}

void UserTimeseries::plot() {
  int i, j, k, ierr, seriesCounter, colorCounter;
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

  QString tempString;
  QStringList dataList = this->m_currentStation->split(",");
  tempString = dataList.value(0);
  int nMarkers = dataList.length();

  long long endDate = this->m_endDateEdit->dateTime().toMSecsSinceEpoch();
  long long startDate = this->m_startDateEdit->dateTime().toMSecsSinceEpoch();

  if (nMarkers > 0) {
    this->m_selectedStations.resize(nMarkers);
    for (i = 0; i < nMarkers; i++) {
      tempString = dataList.value(i);
      this->m_selectedStations[i] = tempString.toInt();
    }
  } else {
    emit timeseriesError(tr("No stations selected"));
    return;
  }

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
    minDate = minDate.addSecs(-offset / 1000);
    maxDate = maxDate.addSecs(-offset / 1000);
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

  seriesCounter = 0;

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
           j < this->m_fileDataUnique[i]->station[this->m_markerId].numSnaps();
           j++) {
        if (this->m_fileDataUnique[i]->station[this->m_markerId].data(j) !=
                MetOceanViewer::NULL_TS &&
            this->m_fileDataUnique[i]->station[this->m_markerId].date(j) >=
                startDate &&
            this->m_fileDataUnique[i]->station[this->m_markerId].date(j) <=
                endDate) {
          TempDate =
              this->m_fileDataUnique[i]->station[this->m_markerId].date(j) +
              addX - offset;
          TempValue =
              this->m_fileDataUnique[i]->station[this->m_markerId].data(j) *
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
    } else {
      //...Plot multiple stations. We use random colors and append the station
      // number
      for (k = 0; k < this->m_selectedStations.length(); k++) {
        if (!this->m_fileDataUnique[i]
                 ->station[this->m_selectedStations[k]]
                 .isNull()) {
          seriesCounter = seriesCounter + 1;
          colorCounter = colorCounter + 1;

          //...Loop the colors
          if (colorCounter >= this->m_randomColorList.length())
            colorCounter = 0;

          series.resize(seriesCounter);
          series[seriesCounter - 1] = new QLineSeries(this->m_chartView);
          series[seriesCounter - 1]->setName(
              this->m_fileDataUnique[i]
                  ->station[this->m_selectedStations[k]]
                  .name() +
              ": " + this->m_table->item(i, 1)->text());
          seriesColor = this->m_randomColorList[colorCounter];
          series[seriesCounter - 1]->setPen(
              QPen(seriesColor, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
          unitConversion = this->m_table->item(i, 3)->text().toDouble();
          addX = this->m_table->item(i, 4)->text().toDouble() * 3.6e+6;
          addY = this->m_table->item(i, 5)->text().toDouble();
          for (j = 0; j < this->m_fileDataUnique[i]
                              ->station[this->m_selectedStations[k]]
                              .numSnaps();
               j++) {
            if (this->m_fileDataUnique[i]
                        ->station[this->m_selectedStations[k]]
                        .data(j) != MetOceanViewer::NULL_TS &&
                this->m_fileDataUnique[i]
                        ->station[this->m_selectedStations[k]]
                        .date(j) >= startDate &&
                this->m_fileDataUnique[i]
                        ->station[this->m_selectedStations[k]]
                        .date(j) <= endDate) {
              TempDate = this->m_fileDataUnique[i]
                             ->station[this->m_selectedStations[k]]
                             .date(j) +
                         addX - offset;
              TempValue = this->m_fileDataUnique[i]
                                  ->station[this->m_selectedStations[k]]
                                  .data(j) *
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
        this->m_fileDataUnique[0]->station[this->m_markerId].name());
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

int UserTimeseries::processData() {
  int ierr, i, j, nRow, InputFileType, dflowLayer;
  double x, y;
  QString StationName, TempFile, TempStationFile, dflowVar;
  QDateTime ColdStart;
  AdcircStationOutput *adcircData;

  nRow = this->m_table->rowCount();

  j = 0;

  for (i = 0; i < nRow; i++) {
    TempFile = this->m_table->item(i, 6)->text();
    InputFileType = Filetypes::getIntegerFiletype(TempFile);
    this->m_allFileData.resize(j + 1);
    this->m_epsg.resize(j + 1);
    this->m_epsg[j] = this->m_table->item(i, 11)->text().toInt();

    if (InputFileType == MetOceanViewer::FileType::ASCII_IMEDS) {
      this->m_allFileData[j] = new Imeds(this);
      ierr = this->m_allFileData[j]->read(TempFile);
      if (ierr != MetOceanViewer::Error::NOERR) {
        this->m_errorString = tr("Error reading file: ") + TempFile;
        return MetOceanViewer::Error::IMEDS_FILEREADERROR;
      }
      this->m_allFileData[j]->success = true;

    } else if (InputFileType == MetOceanViewer::FileType::NETCDF_ADCIRC) {
      ColdStart = QDateTime::fromString(this->m_table->item(i, 7)->text(),
                                        "yyyy-MM-dd hh:mm:ss");
      adcircData = new AdcircStationOutput(this);
      ierr = adcircData->read(TempFile, ColdStart);
      if (ierr != MetOceanViewer::Error::NOERR) {
        this->m_errorString = tr("Error reading file: ") + TempFile;
        return MetOceanViewer::Error::ADCIRC_NETCDFREADERROR;
      }

      this->m_allFileData[j] = adcircData->toIMEDS();
      if (!this->m_allFileData[j]->success)
        return MetOceanViewer::Error::ADCIRC_NETCDFTOIMEDS;
      this->m_allFileData[j]->success = true;

      delete adcircData;

    } else if (InputFileType == MetOceanViewer::FileType::ASCII_ADCIRC) {
      ColdStart = QDateTime::fromString(this->m_table->item(i, 7)->text(),
                                        "yyyy-MM-dd hh:mm:ss");
      TempStationFile = this->m_table->item(i, 10)->text();
      adcircData = new AdcircStationOutput(this);
      ierr = adcircData->read(TempFile, TempStationFile, ColdStart);
      if (ierr != MetOceanViewer::Error::NOERR) {
        this->m_errorString = tr("Error reading file: ") + TempFile;
        return MetOceanViewer::Error::ADCIRC_ASCIIREADERROR;
      }
      this->m_allFileData[j] = adcircData->toIMEDS();
      delete adcircData;

      if (!this->m_allFileData[j]->success)
        return MetOceanViewer::Error::ADCIRC_ASCIITOIMEDS;

    } else if (InputFileType == MetOceanViewer::FileType::NETCDF_DFLOW) {
      this->m_allFileData[j] = new Imeds(this);
      Dflow *dflow = new Dflow(TempFile, this);
      dflowVar = this->m_table->item(i, 12)->text();
      dflowLayer = this->m_table->item(i, 13)->text().toInt();
      ierr = dflow->getVariable(dflowVar, dflowLayer, this->m_allFileData[j]);
      if (ierr != MetOceanViewer::Error::NOERR) {
        this->m_errorString =
            tr("Error processing DFlow: ") + dflow->error->toString();
        return MetOceanViewer::Error::DFLOW_FILEREADERROR;
      }

      delete dflow;

    } else if (InputFileType == MetOceanViewer::FileType::NETCDF_GENERIC) {
      this->m_allFileData[j] = new Imeds(this);
      NetcdfTimeseries *genericNetcdf = new NetcdfTimeseries(this);
      genericNetcdf->setFilename(TempFile);
      ierr = genericNetcdf->read();
      if (ierr != 0) {
        this->m_errorString = "Error processing generic netcdf file.";
        return MetOceanViewer::Error::GENERICNETCDFERROR;
      }
      ierr = genericNetcdf->toImeds(this->m_allFileData[j]);

      delete genericNetcdf;

    } else {
      this->m_errorString = tr("Invalid file format");
      return MetOceanViewer::Error::INVALIDFILEFORMAT;
    }

    if (this->m_allFileData[j]->success)
      j = j + 1;
    else
      return MetOceanViewer::Error::GENERICFILEREADERROR;
  }

  //...Project the data to WGS84
  ierr = this->projectStations(this->m_epsg, this->m_allFileData);
  if (ierr != 0) {
    this->m_errorString = tr("Error projecting the station locations");
    return MetOceanViewer::Error::PROJECTSTATIONS;
  }

  //...Build a unique set of timeseries data
  if (this->m_allFileData.length() == 1) {
    for (i = 0; i < this->m_allFileData[0]->nstations; i++) {
      this->m_xLocations.push_back(
          this->m_allFileData[0]->station[i].coordinate()->longitude());
      this->m_yLocations.push_back(
          this->m_allFileData[0]->station[i].coordinate()->latitude());
      this->m_fileDataUnique = this->m_allFileData;
    }
  } else {
    ierr = this->getUniqueStationList(this->m_allFileData, this->m_xLocations,
                                      this->m_yLocations);
    if (ierr != MetOceanViewer::Error::NOERR) {
      this->m_errorString = tr("Error building the station list");
      return MetOceanViewer::Error::BUILDSTATIONLIST;
    }
  }
  ierr = this->buildRevisedIMEDS(this->m_allFileData, this->m_xLocations,
                                 this->m_yLocations, this->m_fileDataUnique);
  if (ierr != 0) {
    this->m_errorString = tr("Error building the unique dataset");
    return MetOceanViewer::Error::BUILDREVISEDIMEDS;
  }

  //...Delete the data we're done with
  for (i = 0; i < this->m_allFileData.size(); i++) {
    delete this->m_allFileData[i];
  }

  //...Add the markers to the map
  for (i = 0; i < this->m_fileDataUnique[0]->nstations; i++) {
    x = -1.0;
    y = -1.0;
    StationName = "NONAME";

    // Check that we aren't sending a null location to
    // the backend
    for (j = 0; j < this->m_fileDataUnique.length(); j++) {
      if (!this->m_fileDataUnique[j]->station[i].isNull()) {
        StationName = this->m_fileDataUnique[j]->station[i].name();
        x = this->m_fileDataUnique[j]->station[i].coordinate()->longitude();
        y = this->m_fileDataUnique[j]->station[i].coordinate()->latitude();
        break;
      }
    }

    this->m_stationmodel->addMarker(
        Station(QGeoCoordinate(y, x), QString::number(i), StationName));
  }

  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::plotData() {
  //...Get the current marker selections, multiple if user ctrl+click selects
  this->getAsyncMultipleMarkersFromMap();

  return MetOceanViewer::Error::NOERR;
}

//-------------------------------------------//
// Generate a unique list of stations so that
// we can later build a complete list of stations
// and not show data where it doesn't exist for
// certain files
//-------------------------------------------//
int UserTimeseries::getUniqueStationList(QVector<Imeds *> Data,
                                         QVector<double> &X,
                                         QVector<double> &Y) {
  int i, j, k;
  bool found;
  double d;

  for (i = 0; i < Data.length(); i++) {
    for (j = 0; j < Data[i]->nstations; j++) {
      found = false;
      for (k = 0; k < X.length(); k++) {
        d = qSqrt(
            qPow(Data[i]->station[j].coordinate()->longitude() - X[k], 2.0) +
            qPow(Data[i]->station[j].coordinate()->latitude() - Y[k], 2.0));
        if (d < _DUPLICATE_STATION_TOL) {
          found = true;
          break;
        }
      }
      if (!found) {
        X.resize(X.length() + 1);
        Y.resize(Y.length() + 1);
        X[X.length() - 1] = Data[i]->station[j].coordinate()->longitude();
        Y[Y.length() - 1] = Data[i]->station[j].coordinate()->latitude();
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
int UserTimeseries::buildRevisedIMEDS(QVector<Imeds *> &Data, QVector<double> X,
                                      QVector<double> Y,
                                      QVector<Imeds *> &DataOut) {
  int i, j, k;
  bool found;
  double d;

  DataOut.resize(Data.length());

  for (i = 0; i < Data.length(); i++) {
    DataOut[i] = new Imeds(this);

    DataOut[i]->nstations = X.length();
    DataOut[i]->header1 = Data[i]->header1;
    DataOut[i]->header2 = Data[i]->header2;
    DataOut[i]->header3 = Data[i]->header3;
    DataOut[i]->station.resize(X.length());
    for (j = 0; j < X.length(); j++) {
      DataOut[i]->station[j].setLongitude(X[j]);
      DataOut[i]->station[j].setLatitude(Y[j]);
    }
  }

  for (i = 0; i < Data.length(); i++) {
    for (j = 0; j < DataOut[i]->nstations; j++) {
      found = false;
      for (k = 0; k < Data[i]->nstations; k++) {
        d = qSqrt(qPow(Data[i]->station[k].coordinate()->longitude() -
                           DataOut[i]->station[j].coordinate()->longitude(),
                       2.0) +
                  qPow(Data[i]->station[k].coordinate()->latitude() -
                           DataOut[i]->station[j].coordinate()->latitude(),
                       2.0));
        if (d < _DUPLICATE_STATION_TOL) {
          DataOut[i]->station[j].setNumSnaps(Data[i]->station[k].numSnaps());
          DataOut[i]->station[j].setName(Data[i]->station[k].name());
          DataOut[i]->station[j].setData(Data[i]->station[k].allData());
          DataOut[i]->station[j].setDate(Data[i]->station[k].allDate());
          DataOut[i]->station[j].setIsNull(false);
          found = true;
          break;
        }
      }
      if (!found) {
        // Build a station with a null dataset we can find later
        DataOut[i]->station[j].setNumSnaps(1);
        DataOut[i]->station[j].setName("NONAME");
        DataOut[i]->station[j].setData(MetOceanViewer::NULL_TS, 0);
        DataOut[i]->station[j].setDate(0, 0);
        DataOut[i]->station[j].setIsNull(true);
      }
    }
  }
  return MetOceanViewer::Error::NOERR;
}

int UserTimeseries::projectStations(QVector<int> epsg,
                                    QVector<Imeds *> &projectedStations) {
  int i, j, ierr;
  double x, y, x2, y2;
  bool isLatLon;
  proj4 *projection = new proj4(this);

  for (i = 0; i < projectedStations.length(); i++) {
    if (epsg[i] != 4326) {
      for (j = 0; j < projectedStations[i]->nstations; j++) {
        x = projectedStations[i]->station[j].coordinate()->longitude();
        y = projectedStations[i]->station[j].coordinate()->latitude();
        ierr = projection->transform(epsg[i], 4326, x, y, x2, y2, isLatLon);
        if (ierr != 0) return MetOceanViewer::Error::PROJECTSTATIONS;
        projectedStations[i]->station[j].setLongitude(x2);
        projectedStations[i]->station[j].setLatitude(y2);
      }
    }
  }
  delete projection;
  return MetOceanViewer::Error::NOERR;
}
