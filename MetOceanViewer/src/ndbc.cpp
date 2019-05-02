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
#include "ndbc.h"
#include <QDateTimeAxis>
#include <QLegendMarker>
#include <QPixmap>
#include <QPrinter>
#include <QValueAxis>
#include "chartview.h"
#include "ndbcdata.h"

Ndbc::Ndbc(QQuickWidget *inMap, ChartView *inChart, QComboBox *inProductBox,
           QDateTimeEdit *inStartDateEdit, QDateTimeEdit *inEndDateEdit,
           QStatusBar *instatusBar, StationModel *stationModel,
           QString *inSelectedStation, QObject *parent)
    : QObject(parent) {
  this->m_map = inMap;
  this->m_chartView = inChart;
  this->m_productBox = inProductBox;
  this->m_startDateEdit = inStartDateEdit;
  this->m_endDateEdit = inEndDateEdit;
  this->m_statusBar = instatusBar;
  this->m_stationModel = stationModel;
  this->m_selectedStation = inSelectedStation;
  this->m_dataReady = false;
  this->m_data = nullptr;
}

int Ndbc::plotStation() {
  QDateTime startDate = this->m_startDateEdit->dateTime();
  QDateTime endDate = this->m_endDateEdit->dateTime();

  startDate.setTime(QTime(0, 0, 0));
  endDate.setTime(QTime(0, 0, 0));
  endDate = endDate.addDays(1);

  if (startDate >= endDate) {
    emit ndbcError("Invalid date range selected");
    return 1;
  } else if (startDate.date().year() ==
             QDateTime::currentDateTime().date().year()) {
    emit ndbcError(
        "NDBC data only available starting in the prior calendar year.");
    return 1;
  }

  this->m_station =
      this->m_stationModel->findStation(*(this->m_selectedStation));

  NdbcData *n = new NdbcData(this->m_station, startDate, endDate, this);

  this->m_data = new Hmdf(this);
  int ierr = n->get(this->m_data);

  if (ierr != 0) {
    emit ndbcError(n->errorString());
    delete n;
    return ierr;
  }

  delete n;

  this->m_productBox->clear();
  for (int i = 0; i < this->m_data->nstations(); i++) {
    this->m_productBox->addItem(this->m_data->station(i)->name());
  }

  this->plot(0);

  this->m_dataReady = true;

  return 0;
}

bool Ndbc::dataReady() const { return this->m_dataReady; }

int Ndbc::replotStation(int index) { return this->plot(index); }

int Ndbc::plot(int index) {
  HmdfStation *s = this->m_data->station(index);

  this->m_chartView->clear();
  this->m_chartView->initializeAxis(1);

  int offset = Timezone::localMachineOffsetFromUtc() * 1000;

  qint64 minDate, maxDate;
  double ymin, ymax;
  s->dataBounds(minDate, maxDate, ymin, ymax);

  QDateTime startDate = this->m_startDateEdit->dateTime();
  QDateTime endDate = this->m_endDateEdit->dateTime();
  startDate.setTime(QTime(0, 0, 0));
  endDate.addDays(1);
  endDate.setTime(QTime(0, 0, 0));

  QLineSeries *series1 = new QLineSeries(this->m_chartView->chart());
  series1->setName(s->name());
  series1->setPen(
      QPen(QColor(0, 0, 255), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  for (int j = 0; j < s->numSnaps(); j++) {
    series1->append(s->date(j) - offset, s->data(j));
  }

  this->m_chartView->addSeries(series1, s->name());

  this->m_chartView->setDateFormat(startDate, endDate);
  this->m_chartView->setAxisLimits(startDate, endDate, ymin, ymax);

  this->m_chartView->yAxis()->setTitleText("Observed");
  this->m_chartView->dateAxis()->setTitleText("Date (GMT)");

  this->m_chartView->chart()->setTitle("NDBC Station " + this->m_station.id());
  this->m_chartView->initializeLegendMarkers();
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->setStatusBar(this->m_statusBar);

  return 0;
}

int Ndbc::saveImage(QString filename, QString filter) {
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
    QPixmap renderedMap = this->m_map->grab();
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
        this->m_map->size().width() + this->m_chartView->size().width(),
        this->m_map->size().height());
    QRect chartRect(this->m_map->size().width(), 0,
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

int Ndbc::saveData(QString filename) {
  Hmdf *out = new Hmdf(this);
  out->addStation(this->m_data->station(this->m_productBox->currentIndex()));
  out->station(0)->setName(this->m_station.name());

  int ierr = out->write(filename);
  if (ierr != 0) {
    emit ndbcError("Error writing NDBC data to file.");
  }

  delete out;

  return ierr;
}

QString Ndbc::getSelectedMarker() { return this->m_station.id(); }
