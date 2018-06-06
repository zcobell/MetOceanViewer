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
#include "noaa.h"
#include <QGeoRectangle>
#include <QGeoShape>
#include "chartview.h"
#include "generic.h"
#include "hmdf.h"

Noaa::Noaa(QQuickWidget *inMap, ChartView *inChart, QDateEdit *inStartDateEdit,
           QDateEdit *inEndDateEdit, QComboBox *inNoaaProduct,
           QComboBox *inNoaaUnits, QComboBox *inNoaaDatum,
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
  this->m_selectedStation = inSelectedStation;
  this->m_chartView->m_chart = nullptr;

  //...Initialize the station object
  this->m_currentStationData.resize(2);
  this->m_currentStationData[0] = new Hmdf(this);
  this->m_currentStationData[1] = new Hmdf(this);

  //...Initialize the timezone
  this->tz = new Timezone(this);
  tz->fromAbbreviation(this->m_comboTimezone->currentText(),
                       static_cast<TZData::Location>(
                           this->m_comboTimezoneLocation->currentIndex()));
  this->m_offsetSeconds = tz->utcOffset() * 1000;
  this->m_priorOffsetSeconds = this->m_offsetSeconds;
}

Noaa::~Noaa() {}

int Noaa::fetchNOAAData() {
  QEventLoop loop;
  qint64 Duration;
  QString RequestURL, StartString, EndString, Product1, Product2;
  int i, j, ierr, NumDownloads, NumData;
  QVector<QDateTime> StartDateList, EndDateList;

  if (this->m_startDate.operator==(this->m_endDate) ||
      this->m_endDate.operator<(this->m_startDate))
    return MetOceanViewer::Error::NOAA_INVALIDDATERANGE;

  // Begin organizing the dates for download
  QDateTime localStartDate = this->m_startDate.addMSecs(-this->m_offsetSeconds);
  QDateTime localEndDate = this->m_endDate.addMSecs(-this->m_offsetSeconds);
  Duration = localStartDate.daysTo(localEndDate);
  NumDownloads = (Duration / 30) + 1;
  StartDateList.resize(NumDownloads);
  EndDateList.resize(NumDownloads);

  // Build the list of dates in 30 day intervals
  for (i = 0; i < NumDownloads; i++) {
    StartDateList[i] = localStartDate.addDays(i * 30).addDays(i);
    StartDateList[i].setTime(QTime(0, 0, 0));
    EndDateList[i] = StartDateList[i].addDays(30);
    EndDateList[i].setTime(QTime(23, 59, 59));
    if (EndDateList[i] > localEndDate) EndDateList[i] = localEndDate;
  }

  ierr = this->getNoaaProductId(Product1, Product2);
  if (ierr != 0) return ierr;

  if (this->m_productIndex == 0)
    NumData = 2;
  else
    NumData = 1;

  if (this->m_productIndex == 4 || this->m_productIndex == 5 ||
      this->m_productIndex == 6 || this->m_productIndex == 7 ||
      this->m_productIndex == 8)
    this->m_datum = "Stnd";

  // Allocate the NOAA array
  this->m_webData.clear();
  this->m_webData.resize(NumData);
  for (i = 0; i < NumData; i++) this->m_webData[i].resize(NumDownloads);

  QNetworkAccessManager *manager = new QNetworkAccessManager(this);

  for (j = 0; j < NumData; j++) {
    for (i = 0; i < NumDownloads; i++) {
      // Make the date string
      StartString = StartDateList[i].toString("yyyyMMdd hh:mm");
      EndString = EndDateList[i].toString("yyyyMMdd hh:mm");

      // Build the URL to request data from the NOAA CO-OPS API
      if (j == 0)
        RequestURL =
            QString("http://tidesandcurrents.noaa.gov/api/datagetter?") +
            QString("product=" + Product1 + "&application=metoceanviewer") +
            QString("&begin_date=") + StartString + QString("&end_date=") +
            EndString + QString("&station=") + this->m_station.id() +
            QString("&time_zone=GMT&units=") + this->m_units +
            QString("&interval=&format=csv");
      else
        RequestURL =
            QString("http://tidesandcurrents.noaa.gov/api/datagetter?") +
            QString("product=" + Product2 + "&application=metoceanviewer") +
            QString("&begin_date=") + StartString + QString("&end_date=") +
            EndString + QString("&station=") + this->m_station.id() +
            QString("&time_zone=GMT&units=") + this->m_units +
            QString("&interval=&format=csv");

      // Allow a different datum where allowed
      if (this->m_datum != "Stnd")
        RequestURL = RequestURL + QString("&datum=") + this->m_datum;

      // Send the request
      QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(RequestURL)));
      connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
      connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
              SLOT(quit()));
      loop.exec();
      //...Check for a redirect from NOAA. This fixes bug #26
      QVariant redirectionTargetURL =
          reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
      if (!redirectionTargetURL.isNull()) {
        QNetworkReply *reply2 =
            manager->get(QNetworkRequest(redirectionTargetURL.toUrl()));
        connect(reply2, SIGNAL(finished()), &loop, SLOT(quit()));
        connect(reply2, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
                SLOT(quit()));
        loop.exec();
        reply->deleteLater();
        this->readNOAAResponse(reply2, i, j);
      } else
        this->readNOAAResponse(reply, i, j);
    }
  }

  this->m_loadedStationId = this->m_station.id().toInt();

  return 0;
}

int Noaa::formatNOAAResponse(QVector<QByteArray> input, QString &error,
                             int index) {
  QString TempData, DateS, YearS, MonthS, DayS, HourMinS, HourS, MinS, WLS;
  QStringList TimeSnap;
  QVector<QString> InputData;
  QVector<QStringList> DataList;
  QVector<QString> Temp;
  QDateTime tempDate;

  InputData.resize(input.length());
  DataList.resize(input.length());
  Temp.resize(input.length());

  for (int i = 0; i < DataList.length(); i++) {
    InputData[i] = QString(input[i]);
    DataList[i] =
        InputData[i].split(QRegExp("[\r\n]"), QString::SkipEmptyParts);
    Temp[i] = QString(input[i]);
    error = Temp[i] + "\n";
  }

  for (int j = 0; j < DataList.length(); j++) {
    for (int i = 1; i < DataList[j].length(); i++) {
      TempData = DataList[j].value(i);
      TimeSnap = TempData.split(",");
      DateS = TimeSnap.value(0);
      YearS = DateS.mid(0, 4);
      MonthS = DateS.mid(5, 2);
      DayS = DateS.mid(8, 2);
      HourMinS = DateS.mid(11, 5);
      HourS = HourMinS.mid(0, 2);
      MinS = HourMinS.mid(3, 2);
      WLS = TimeSnap.value(1);
      tempDate.setDate(QDate(YearS.toInt(), MonthS.toInt(), DayS.toInt()));
      tempDate.setTime(QTime(HourS.toInt(), MinS.toInt(), 0));
      this->m_currentStationData[index]->station(0)->setNext(
          tempDate.toMSecsSinceEpoch(), WLS.toDouble());
    }
  }

  return 0;
}

int Noaa::getDataBounds(double &ymin, double &ymax) {
  ymin = DBL_MAX;
  ymax = -DBL_MAX;

  for (int i = 0; i < this->m_currentStationData.length(); i++) {
    for (int j = 0; j < this->m_currentStationData[i]->station(0)->numSnaps();
         j++) {
      if (this->m_currentStationData[i]->station(0)->data(j) != 0.0) {
        if (this->m_currentStationData[i]->station(0)->data(j) < ymin)
          ymin = this->m_currentStationData[i]->station(0)->data(j);
        if (this->m_currentStationData[i]->station(0)->data(j) > ymax)
          ymax = this->m_currentStationData[i]->station(0)->data(j);
      }
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
                                                 << "mb";
  static QStringList unitsImperial = QStringList() << "ft"
                                                   << "ft"
                                                   << "ft"
                                                   << "ft"
                                                   << "F"
                                                   << "F"
                                                   << "knot"
                                                   << "%"
                                                   << "mb";
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
    this->m_ylabel = product + " (" + this->getUnitsLabel() + ", " +
                     this->getDatumLabel() + ")";
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
  QString S1, S2, format;
  QDateTime minDateTime, maxDateTime;

  maxDateTime = QDateTime(QDate(1000, 1, 1), QTime(0, 0, 0));
  minDateTime = QDateTime(QDate(3000, 1, 1), QTime(0, 0, 0));

  //...Create the line series
  this->generateLabels();
  this->getNoaaProductSeriesNaming(S1, S2);
  this->getDataBounds(ymin, ymax);

  this->m_currentStationData[0]->units() = this->m_units;
  this->m_currentStationData[0]->datum() = this->m_datum;
  this->m_currentStationData[1]->units() = this->m_units;
  this->m_currentStationData[1]->datum() = this->m_datum;

  //...Create the chart
  this->m_chartView->m_chart = new QChart();

  QLineSeries *series1 = new QLineSeries(this);
  QLineSeries *series2 = new QLineSeries(this);
  series1->setName(S1);
  series2->setName(S2);
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  series2->setPen(
      QPen(QColor(0, 255, 0), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  minDateTime = this->m_startDateEdit->dateTime();
  maxDateTime = this->m_endDateEdit->dateTime().addDays(1);

  minDateTime.setTime(QTime(0, 0, 0));
  maxDateTime.setTime(QTime(0, 0, 0));

  QDateTimeAxis *axisX = new QDateTimeAxis(this);
  axisX->setTickCount(5);
  if (this->m_startDate.daysTo(this->m_endDate) > 90)
    axisX->setFormat("MM/yyyy");
  else if (this->m_startDate.daysTo(this->m_endDate) > 4)
    axisX->setFormat("MM/dd/yyyy");
  else
    axisX->setFormat("MM/dd/yyyy hh:mm");
  axisX->setTitleText("Date (" + this->tz->abbreviation() + ")");
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

  for (int j = 0; j < this->m_currentStationData[0]->station(0)->numSnaps();
       j++) {
    if (QDateTime::fromMSecsSinceEpoch(
            this->m_currentStationData[0]->station(0)->date(j) +
            this->m_offsetSeconds)
            .isValid()) {
      if (this->m_currentStationData[0]->station(0)->data(j) != 0.0)
        series1->append(this->m_currentStationData[0]->station(0)->date(j) +
                            this->m_offsetSeconds,
                        this->m_currentStationData[0]->station(0)->data(j));
    }
  }
  this->m_chartView->m_chart->addSeries(series1);
  series1->attachAxis(axisX);
  series1->attachAxis(axisY);
  this->m_chartView->clear();
  this->m_chartView->addSeries(series1, series1->name());

  if (this->m_productIndex == 0) {
    for (int j = 0; j < this->m_currentStationData[1]->station(0)->numSnaps();
         j++)
      if (QDateTime::fromMSecsSinceEpoch(
              this->m_currentStationData[1]->station(0)->date(j) +
              this->m_offsetSeconds)
              .isValid()) {
        if (this->m_currentStationData[1]->station(0)->data(j) != 0.0)
          series2->append(this->m_currentStationData[1]->station(0)->date(j) +
                              this->m_offsetSeconds,
                          this->m_currentStationData[1]->station(0)->data(j));
      }
    this->m_chartView->m_chart->addSeries(series2);
    this->m_chartView->addSeries(series2, series2->name());
    series2->attachAxis(axisX);
    series2->attachAxis(axisY);
  }

  for (int i = 0; i < this->m_chartView->m_chart->legend()->markers().length();
       i++)
    this->m_chartView->m_chart->legend()->markers().at(i)->setFont(
        QFont("Helvetica", 10, QFont::Bold));

  axisY->setTickCount(10);
  axisY->applyNiceNumbers();
  axisX->setGridLineColor(QColor(200, 200, 200));
  axisY->setGridLineColor(QColor(200, 200, 200));
  axisY->setShadesPen(Qt::NoPen);
  axisY->setShadesBrush(QBrush(QColor(240, 240, 240)));
  axisY->setShadesVisible(true);

  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);
  this->m_chartView->m_chart->setTitle(
      tr("NOAA Station ") + this->m_station.id() + ": " +
      this->m_currentStationData[0]->station(0)->name());
  this->m_chartView->m_chart->setTitleFont(QFont("Helvetica", 14, QFont::Bold));
  this->m_chartView->setRenderHint(QPainter::Antialiasing);

  this->m_chartView->setChart(this->m_chartView->m_chart);

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

  return 0;
}

int Noaa::plotNOAAStation() {
  if (*(this->m_selectedStation) == "-1") {
    emit noaaError(tr("You must select a station"));
    return 1;
  } else {
    this->m_station =
        this->m_stationModel->findStation(*(this->m_selectedStation));
    this->m_station.id() = this->m_station.id().toInt();

    HmdfStation *station1 = new HmdfStation(this);
    HmdfStation *station2 = new HmdfStation(this);

    station1->setLongitude(this->m_station.coordinate().longitude());
    station1->setLatitude(this->m_station.coordinate().latitude());
    station1->setName(this->m_station.name());
    station1->setId("NOAA_" + this->m_station.id());
    station1->setStationIndex(0);

    station2->setLongitude(this->m_station.coordinate().longitude());
    station2->setLatitude(this->m_station.coordinate().latitude());
    station2->setName(this->m_station.name());
    station2->setId("NOAA_" + this->m_station.id());
    station2->setStationIndex(0);

    this->m_currentStationData[0]->addStation(station1);
    this->m_currentStationData[1]->addStation(station2);

    //...Grab the options from the UI
    this->m_startDate = this->m_startDateEdit->dateTime();
    this->m_startDate.setTime(QTime(0, 0, 0));
    this->m_endDate = this->m_endDateEdit->dateTime();
    this->m_endDate = this->m_endDate.addDays(1);
    this->m_endDate.setTime(QTime(0, 0, 0));
    this->m_units = this->m_comboUnits->currentText();
    this->m_datum = this->m_comboDatum->currentText();
    this->m_productIndex = this->m_comboProduct->currentIndex();

    // Update status
    this->m_statusBar->showMessage(tr("Downloading data from NOAA...", 0));

    int ierr = this->fetchNOAAData();
    if (ierr != MetOceanViewer::Error::NOERR) return ierr;

    //...Update the status bar
    this->m_statusBar->showMessage(tr("Plotting the data from NOAA..."));

    //...Generate prep the data for plotting
    ierr = this->prepNOAAResponse();
    if (ierr != MetOceanViewer::Error::NOERR) return ierr;

    //...Check for valid data
    if (this->m_currentStationData[0]->station(0)->numSnaps() < 5) {
      emit noaaError(this->m_errorStringVec[0]);
      return 1;
    }

    //...Plot the chart
    ierr = this->plotChart();
    if (ierr != MetOceanViewer::Error::NOERR) return ierr;

    this->m_statusBar->clearMessage();

    return 0;
  }
}

int Noaa::prepNOAAResponse() {
  QVector<QString> NOAAData;
  int i;

  NOAAData.resize(m_webData.length());
  this->m_errorStringVec.resize(m_webData.length());
  for (i = 0; i < m_webData.length(); i++) {
    NOAAData[i] = this->formatNOAAResponse(this->m_webData[i],
                                           this->m_errorStringVec[i], i);
    this->m_errorStringVec[i].remove(QRegExp("[\\n\\t\\r]"));
    if (this->m_errorStringVec[i] ==
        QStringLiteral(
            " Wrong Date: The end date should be greater than the begin date "))
      this->m_errorStringVec[i] =
          QStringLiteral("NOAA data unavailable for the specified dates");
  }
  return 0;
}

void Noaa::readNOAAResponse(QNetworkReply *reply, int index, int index2) {
  QByteArray Data;

  // Catch some errors during the download
  if (reply->error() != 0) {
    emit noaaError(tr("ERROR: ") + reply->errorString());
    reply->deleteLater();
    return;
  }

  // Read the data received from NOAA server
  Data = reply->readAll();

  // Save the data into an array and increment the counter
  this->m_webData[index2][index] = Data;

  // Delete this response
  reply->deleteLater();

  return;
}

int Noaa::getNoaaProductId(QString &product1, QString &product2) {
  static QStringList noaaProductCode = QStringList() << "water_level"
                                                     << "hourly_height"
                                                     << "predictions"
                                                     << "air_temperature"
                                                     << "water_temperature"
                                                     << "wind"
                                                     << "humidity"
                                                     << "air_pressure";
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
                    << "Air Pressure";
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
                                    << "Air Pressure";
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

int Noaa::saveNOAAData(QString filename, QString PreviousDirectory,
                       QString format) {
  QString filename2;

  for (int index = 0; index < this->m_currentStationData.length(); index++) {
    if (this->m_currentStationData.length() == 2) {
      if (index == 0)
        filename2 = PreviousDirectory + "/Observation_" + filename;
      else
        filename2 = PreviousDirectory + "/Predictions_" + filename;
    } else
      filename2 = PreviousDirectory + "/" + filename;

    int ierr = this->m_currentStationData[index]->write(filename2);
    if (ierr != 0) emit noaaError("Error writing NOAA data to file");
  }

  return 0;
}

int Noaa::replotChart(Timezone *newTimezone) {
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

  QDateTime minDateTime = this->m_startDateEdit->dateTime();
  QDateTime maxDateTime = this->m_endDateEdit->dateTime().addDays(1);
  minDateTime.setTime(QTime(0, 0, 0));
  maxDateTime.setTime(QTime(0, 0, 0));
  minDateTime.setTimeSpec(Qt::UTC);
  maxDateTime.setTimeSpec(Qt::UTC);

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
