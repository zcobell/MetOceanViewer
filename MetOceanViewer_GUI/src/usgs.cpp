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
#include "station.h"

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

int Usgs::fetchUSGSData() {
  //...Get the current marker
  QNetworkAccessManager *manager = new QNetworkAccessManager(this);
  QString endDateString1, startDateString1;
  QString endDateString2, startDateString2;
  QString RequestURL;
  QEventLoop loop;
  int i, ierr;

  if (this->m_currentStation.id() == QString()) {
    emit usgsError("You must select a station");
    return 1;
  }

  //...Format the date strings
  endDateString1 =
      "&endDT=" + this->m_requestEndDate.addDays(1).toString("yyyy-MM-dd");
  startDateString1 =
      "&startDT=" + this->m_requestStartDate.toString("yyyy-MM-dd");
  endDateString2 =
      "&end_date=" + this->m_requestEndDate.addDays(1).toString("yyyy-MM-dd");
  startDateString2 =
      "&begin_date=" + this->m_requestStartDate.toString("yyyy-MM-dd");

  //...Construct the correct request URL
  if (this->m_usgsDataMethod == 0)
    RequestURL = "https://waterdata.usgs.gov/nwis/uv?format=rdb&site_no=" +
                 this->m_currentStation.id() + startDateString2 +
                 endDateString2;
  else if (this->m_usgsDataMethod == 1)
    RequestURL = "https://waterservices.usgs.gov/nwis/iv/?sites=" +
                 this->m_currentStation.id() + startDateString1 +
                 endDateString1 + "&format=rdb";
  else
    RequestURL = "https://waterservices.usgs.gov/nwis/dv/?sites=" +
                 this->m_currentStation.id() + startDateString1 +
                 endDateString1 + "&format=rdb";

  //...Make the request to the server
  QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(RequestURL)));
  connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  connect(reply, SIGNAL(error(QNetworkReply::NetworkError)), &loop,
          SLOT(quit()));
  loop.exec();

  if (reply->error() != QNetworkReply::NoError) {
    emit usgsError(tr("There was an error contacting the USGS data server"));
    return 1;
  }

  //...Read the response
  ierr = this->readUSGSDataFinished(reply);
  if (ierr != 0) {
    emit usgsError(tr("Error reading the USGS data"));
    return 1;
  }

  //...Update the combo box
  for (i = 0; i < this->m_availableDatatypes.length(); i++)
    m_comboProduct->addItem(this->m_availableDatatypes[i]);
  m_comboProduct->setCurrentIndex(0);
  this->m_productName = m_comboProduct->currentText();

  //...Plot the first series
  ierr = this->plotUSGS();
  if (ierr != 0) {
    emit usgsError(tr("No data available for this station"));
    return 1;
  }

  //...Restore the status bar
  this->m_statusBar->clearMessage();

  return 0;
}

int Usgs::formatUSGSInstantResponse(QByteArray Input) {
  bool doubleok;
  int ParamStart, ParamStop;
  int HeaderEnd;
  double TempData;
  QStringList TempList;
  QString TempLine, TempDateString, TempTimeZoneString;
  QDateTime CurrentDate;

  QString InputData(Input);
  QStringList SplitByLine =
      InputData.split(QRegExp("[\n]"), QString::SkipEmptyParts);

  ParamStart = -1;
  ParamStop = -1;
  HeaderEnd = -1;

  if (InputData.isEmpty() || InputData.isNull()) {
    this->m_errorString =
        tr("This data is not available except from the USGS archive server.");
    return MetOceanViewer::Error::USGS_ARCHIVEONLY;
  }

  //...Save the potential error string
  this->m_errorString = InputData.remove(QRegExp("[\n\t\r]"));

  //...Start by finding the header and reading the parameters from it
  for (int i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(15) == "# Data provided") {
      ParamStart = i + 2;
      break;
    }
  }

  for (int i = ParamStart; i < SplitByLine.length(); i++) {
    TempLine = SplitByLine.value(i);
    if (TempLine == "#") {
      ParamStop = i - 1;
      break;
    }
  }

  this->m_availableDatatypes.resize(ParamStop - ParamStart + 1);

  for (int i = ParamStart; i <= ParamStop; i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(" ", QString::SkipEmptyParts);
    this->m_availableDatatypes[i - ParamStart] = QString();
    for (int j = 3; j < TempList.length(); j++) {
      if (j == 3)
        this->m_availableDatatypes[i - ParamStart] = TempList.value(j);
      else
        this->m_availableDatatypes[i - ParamStart] =
            this->m_availableDatatypes[i - ParamStart] + " " +
            TempList.value(j);
    }
  }

  //...Find out where the header ends
  for (int i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(1) != "#") {
      HeaderEnd = i + 2;
      break;
    }
  }

  //...Initialize the array
  this->m_selectedProductData.resize(this->m_availableDatatypes.length());

  //...Sanity check
  if (this->m_selectedProductData.length() == 0) return -1;

  //...Zero counters
  for (int i = 0; i < this->m_selectedProductData.length(); i++)
    this->m_selectedProductData[i].m_numDataPoints = 0;

  //...Read the data into the array
  for (int i = HeaderEnd; i < SplitByLine.length(); i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(QRegExp("[\t]"));
    TempDateString = TempList.value(2);
    TempTimeZoneString = TempList.value(3);
    CurrentDate = QDateTime::fromString(TempDateString, "yyyy-MM-dd hh:mm");
    CurrentDate.setTimeSpec(Qt::UTC);
    int OffsetHours = this->getTimezoneOffset(TempTimeZoneString);
    CurrentDate = CurrentDate.addSecs(-3600 * OffsetHours);
    for (int j = 0; j < this->m_availableDatatypes.length(); j++) {
      TempData = TempList.value(2 * j + 4).toDouble(&doubleok);
      if (!TempList.value(2 * j + 4).isNull() && doubleok) {
        this->m_selectedProductData[j].m_numDataPoints =
            this->m_selectedProductData[j].m_numDataPoints + 1;
        this->m_selectedProductData[j].m_data.resize(
            this->m_selectedProductData[j].m_data.length() + 1);
        this->m_selectedProductData[j].m_date.resize(
            this->m_selectedProductData[j].m_date.length() + 1);
        this->m_selectedProductData[j]
            .m_data[this->m_selectedProductData[j].m_data.length() - 1] =
            TempData;
        this->m_selectedProductData[j]
            .m_date[this->m_selectedProductData[j].m_date.length() - 1] =
            CurrentDate;
      }
    }
  }

  return 0;
}

int Usgs::formatUSGSDailyResponse(QByteArray Input) {
  int i, j, ParamStart, ParamStop;
  int HeaderEnd;
  double TempData;
  QStringList TempList;
  QString TempLine, TempDateString;
  QString InputData(Input);
  QStringList SplitByLine =
      InputData.split(QRegExp("[\n]"), QString::SkipEmptyParts);
  QDateTime CurrentDate;
  bool doubleok;

  ParamStart = -1;
  ParamStop = -1;
  HeaderEnd = -1;

  //...Save the potential error string
  this->m_errorString = InputData.remove(QRegExp("[\n\t\r]"));

  //...Start by finding the header and reading the parameters from it
  for (i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(15) == "# Data provided") {
      ParamStart = i + 2;
      break;
    }
  }

  for (i = ParamStart; i < SplitByLine.length(); i++) {
    TempLine = SplitByLine.value(i);
    if (TempLine == "#") {
      ParamStop = i - 1;
      break;
    }
  }

  this->m_availableDatatypes.resize(ParamStop - ParamStart + 1);

  for (i = ParamStart; i <= ParamStop; i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(" ", QString::SkipEmptyParts);
    this->m_availableDatatypes[i - ParamStart] = QString();
    for (j = 3; j < TempList.length(); j++) {
      if (j == 3)
        this->m_availableDatatypes[i - ParamStart] = TempList.value(j);
      else
        this->m_availableDatatypes[i - ParamStart] =
            this->m_availableDatatypes[i - ParamStart] + " " +
            TempList.value(j);
    }
  }

  //...Remove the leading number
  for (i = 0; i < this->m_availableDatatypes.length(); i++)
    m_availableDatatypes[i] = m_availableDatatypes[i].mid(6).simplified();

  //...Find out where the header ends
  for (i = 0; i < SplitByLine.length(); i++) {
    if (SplitByLine.value(i).left(1) != "#") {
      HeaderEnd = i + 2;
      break;
    }
  }

  //...Delete the old data
  for (i = 0; i < this->m_selectedProductData.length(); i++) {
    this->m_selectedProductData[i].m_data.clear();
    this->m_selectedProductData[i].m_date.clear();
    this->m_selectedProductData[i].m_numDataPoints = 0;
  }
  this->m_selectedProductData.clear();

  //...Initialize the array
  this->m_selectedProductData.resize(m_availableDatatypes.length());

  //...Zero counters
  for (i = 0; i < this->m_selectedProductData.length(); i++)
    this->m_selectedProductData[i].m_numDataPoints = 0;

  //...Read the data into the array
  for (i = HeaderEnd; i < SplitByLine.length(); i++) {
    TempLine = SplitByLine.value(i);
    TempList = TempLine.split(QRegExp("[\t]"));
    TempDateString = TempList.value(2);
    CurrentDate = QDateTime::fromString(TempDateString, "yyyy-MM-dd");
    CurrentDate.setTimeSpec(Qt::UTC);
    for (j = 0; j < m_availableDatatypes.length(); j++) {
      TempData = TempList.value(2 * j + 3).toDouble(&doubleok);
      if (!TempList.value(2 * j + 3).isNull() && doubleok) {
        this->m_selectedProductData[j].m_numDataPoints =
            this->m_selectedProductData[j].m_numDataPoints + 1;
        this->m_selectedProductData[j].m_data.resize(
            this->m_selectedProductData[j].m_data.length() + 1);
        this->m_selectedProductData[j].m_date.resize(
            this->m_selectedProductData[j].m_date.length() + 1);
        this->m_selectedProductData[j]
            .m_data[this->m_selectedProductData[j].m_data.length() - 1] =
            TempData;
        this->m_selectedProductData[j]
            .m_date[this->m_selectedProductData[j].m_date.length() - 1] =
            CurrentDate;
      }
    }
  }

  this->m_usgsDataReady = true;

  return 0;
}

int Usgs::getDataBounds(double &ymin, double &ymax) {
  int j;

  ymin = 999999999.0;
  ymax = -999999999.0;

  for (j = 0; j < this->m_allStationData.length(); j++) {
    if (this->m_allStationData[j].m_data < ymin)
      ymin = this->m_allStationData[j].m_data;
    if (this->m_allStationData[j].m_data > ymax)
      ymax = this->m_allStationData[j].m_data;
  }
  return 0;
}

int Usgs::getTimezoneOffset(QString timezone) {
  if (timezone.isNull() || timezone.isEmpty()) return 0;
  Timezone tempTZ;
  tempTZ.fromAbbreviation(timezone, TZData::NorthAmerica);
  return tempTZ.utcOffset();
}

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
    ierr = this->fetchUSGSData();
    if (ierr != 0) return ierr;
  }

  return 0;
}

int Usgs::replotCurrentUSGSStation(int index) {
  if (this->m_usgsDataReady) {
    this->m_productIndex = index;
    this->m_productName = m_comboProduct->currentText();
    int ierr = this->plotUSGS();
    m_statusBar->clearMessage();
    if (ierr != 0) {
      this->m_errorString = tr("No data available for this selection.");
      return ierr;
    }
    return 0;
  }
  return 0;
}

int Usgs::plotUSGS() {
  double ymin, ymax;
  QString format;

  // Put the data into a plotting object
  this->m_allStationData.resize(
      this->m_selectedProductData[this->m_productIndex].m_date.length());
  for (int i = 0;
       i < this->m_selectedProductData[this->m_productIndex].m_date.length();
       i++) {
    this->m_allStationData[i].m_date =
        this->m_selectedProductData[this->m_productIndex].m_date[i].date();
    this->m_allStationData[i].m_time =
        this->m_selectedProductData[this->m_productIndex].m_date[i].time();
    this->m_allStationData[i].m_data =
        this->m_selectedProductData[this->m_productIndex].m_data[i];
  }

  if (this->m_allStationData.length() < 5) return -1;

  //...Create the line series
  int ierr = this->getDataBounds(ymin, ymax);
  if (ierr != 0) return ierr;

  QLineSeries *series1 = new QLineSeries(this);
  series1->setName(this->m_productName);
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  //...Create the chart
  this->m_chartView->m_chart = new QChart();
  this->m_chartView->m_chart->setAnimationOptions(QChart::SeriesAnimations);
  this->m_chartView->m_chart->legend()->setAlignment(Qt::AlignBottom);

  for (int j = 0; j < this->m_allStationData.length(); j++) {
    if (QDateTime(this->m_allStationData[j].m_date,
                  this->m_allStationData[j].m_time)
            .isValid()) {
      series1->append(QDateTime(this->m_allStationData[j].m_date,
                                this->m_allStationData[j].m_time)
                          .toMSecsSinceEpoch(),
                      this->m_allStationData[j].m_data);
    }
  }
  this->m_chartView->m_chart->addSeries(series1);

  this->m_chartView->clear();
  this->m_chartView->addSeries(series1, this->m_productName);

  QDateTime minDateTime =
      QDateTime(m_allStationData[0].m_date, m_allStationData[0].m_time);
  QDateTime maxDateTime =
      QDateTime(m_allStationData[m_allStationData.length() - 1].m_date,
                m_allStationData[m_allStationData.length() - 1].m_time);
  minDateTime.setTimeSpec(Qt::UTC);
  maxDateTime.setTimeSpec(Qt::UTC);

  minDateTime =
      QDateTime(minDateTime.date(), QTime(minDateTime.time().hour(), 0, 0));
  maxDateTime =
      QDateTime(maxDateTime.date(), QTime(maxDateTime.time().hour() + 1, 0, 0));

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
  axisY->setTitleText(this->m_productName.split("),QStringLiteral(").value(0));
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

int Usgs::readUSGSDataFinished(QNetworkReply *reply) {
  int ierr;

  if (reply->error() != QNetworkReply::NoError) {
    this->m_errorString = reply->errorString();
    return MetOceanViewer::Error::USGS_SERVERREADERROR;
  }

  // Read the data that was received
  QByteArray RawUSGSData = reply->readAll();

  // Put the data into an array with all variables
  if (this->m_usgsDataMethod == 0 || this->m_usgsDataMethod == 1)
    ierr = this->formatUSGSInstantResponse(RawUSGSData);
  else
    ierr = this->formatUSGSDailyResponse(RawUSGSData);
  if (ierr != 0) return MetOceanViewer::Error::USGS_FORMATTING;

  this->m_usgsDataReady = true;

  // Delete the QNetworkReply object off the heap
  reply->deleteLater();

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
  QFile USGSOutput(filename);
  QTextStream Output(&USGSOutput);
  USGSOutput.open(QIODevice::WriteOnly);

  if (format.compare("CSV") == 0) {
    Output << "Station: " + this->m_currentStation.id() + "\n";
    Output << "Datum: N/A\n";
    Output << "Units: N/A\n";
    Output << "\n";
    for (int i = 0; i < this->m_allStationData.length(); i++) {
      Output << this->m_allStationData[i].m_date.toString("MM/dd/yyyy") +
                    "),QStringLiteral(" +
                    this->m_allStationData[i].m_time.toString("hh:mm") +
                    "),QStringLiteral(" +
                    QString::number(this->m_allStationData[i].m_data) + "\n";
    }
  } else if (format.compare("IMEDS") == 0) {
    Output << "% IMEDS generic format - Water Level\n";
    Output << "% year month day hour min sec value\n";
    Output << "USGS   UTC    N/A\n";
    Output << "USGS_" + this->m_currentStation.id() + "   " +
                  QString::number(
                      this->m_currentStation.coordinate().latitude()) +
                  "   " +
                  QString::number(
                      this->m_currentStation.coordinate().longitude()) +
                  "\n";
    for (int i = 0; i < this->m_allStationData.length(); i++) {
      Output << this->m_allStationData[i].m_date.toString("yyyy") + "    " +
                    this->m_allStationData[i].m_date.toString("MM") + "    " +
                    this->m_allStationData[i].m_date.toString("dd") + "    " +
                    this->m_allStationData[i].m_time.toString("hh") + "    " +
                    this->m_allStationData[i].m_time.toString("mm") + "    " +
                    "00" + "    " +
                    QString::number(this->m_allStationData[i].m_data) + "\n";
    }
  } else if (format.compare("netCDF") == 0) {
    emit usgsError("netCDF format not implemented yet.");
  }
  USGSOutput.close();

  return 0;
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

  QDateTime minDateTime =
      QDateTime(m_allStationData[0].m_date, m_allStationData[0].m_time);
  QDateTime maxDateTime =
      QDateTime(m_allStationData[m_allStationData.length() - 1].m_date,
                m_allStationData[m_allStationData.length() - 1].m_time);
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
