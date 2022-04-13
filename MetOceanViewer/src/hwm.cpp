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
#include "hwm.h"
#include <float.h>
#include <QPrinter>
#include "colors.h"
#include "generic.h"

Hwm::Hwm(QLineEdit *inFilebox, QCheckBox *inManualCheck,
         QComboBox *inUnitCombobox, QCheckBox *inForceThroughZero,
         QCheckBox *inUpperLowerLines, QCheckBox *inColorHWMDots,
         QPushButton *inHWMColor, QPushButton *inButton121LineColor,
         QPushButton *inButtonBoundingLineColor,
         QPushButton *inButtonRegLineColor, QLineEdit *inModeledAxisLabel,
         QLineEdit *inMeasuredAxisLabel, QLineEdit *inPlotTitle,
         QSpinBox *inBoundingLinesValue, QQuickWidget *inMap,
         ChartView *inChartView, QStatusBar *inStatusBar,
         QVector<double> &inClassValues, StationModel *stationModel,
         QObject *parent)
    : QObject(parent) {
  this->m_fileBox = inFilebox;
  this->m_checkUserClasses = inManualCheck;
  this->m_comboUnits = inUnitCombobox;
  this->m_checkForceZero = inForceThroughZero;
  this->m_buttonColorHwm = inHWMColor;
  this->m_buttonColor121Line = inButton121LineColor;
  this->m_buttonColorBoundingLine = inButtonBoundingLineColor;
  this->m_buttonColorRegLine = inButtonRegLineColor;
  this->m_modeledAxisLabelBox = inModeledAxisLabel;
  this->m_measuredAxisLabelBox = inMeasuredAxisLabel;
  this->m_checkStdDevLines = inUpperLowerLines;
  this->m_boundingLineValue = inBoundingLinesValue;
  this->m_checkColorDots = inColorHWMDots;
  this->m_plotTitleBox = inPlotTitle;
  this->m_chartView = inChartView;
  this->m_quickMap = inMap;
  this->m_statusBar = inStatusBar;
  this->m_stationModel = stationModel;
  this->m_classes = inClassValues;
  this->m_hwm = new HighWaterMarks(this);
}

QString Hwm::getErrorString() { return this->m_errorString; }

int Hwm::classifyHWM(double diff) {
  for (int i = 0; i < 7; i++) {
    if (diff < this->m_classes[i]) return i;
  }
  return 7;
}

int Hwm::plotHWMMap() {
  QString unitString;

  this->m_stationModel->clear();

  if (this->m_comboUnits->currentIndex() == 1)
    unitString = "m";
  else
    unitString = "ft";

  for (int i = 0; i < this->m_hwm->n(); ++i) {
    int classification;
    if (this->m_hwm->hwm(i)->modeledElevation() < -999)
      classification = -1;
    else
      classification = this->classifyHWM(this->m_hwm->hwm(i)->modeledError());

    Station s = Station(
        QGeoCoordinate(this->m_hwm->hwm(i)->coordinate()->latitude(),
                       this->m_hwm->hwm(i)->coordinate()->longitude()),
        QString::number(i), "hwm", this->m_hwm->hwm(i)->observedElevation(),
        this->m_hwm->hwm(i)->modeledElevation(), classification);
    this->m_stationModel->addMarker(s);
  }

  StationModel::fitMarkers(this->m_quickMap, this->m_stationModel);

  QObject *mapObject = this->m_quickMap->rootObject();
  QMetaObject::invokeMethod(
      mapObject, "showLegend", Q_ARG(QVariant, this->m_classes[0]),
      Q_ARG(QVariant, this->m_classes[1]), Q_ARG(QVariant, this->m_classes[2]),
      Q_ARG(QVariant, this->m_classes[3]), Q_ARG(QVariant, this->m_classes[4]),
      Q_ARG(QVariant, this->m_classes[5]), Q_ARG(QVariant, this->m_classes[6]),
      Q_ARG(QVariant, unitString));

  return 0;
}

int Hwm::plotRegression() {
  QString RegressionTitle, XLabel, YLabel;
  QString RegressionString, CorrelationString, StandardDeviationString;
  QColor HWMColor, One2OneColor, BoundColor, RegColor;
  double boundValue, min, max;
  bool displayBoundingLines, doColorDots;
  QVector<QColor> dotColors;

  this->m_chartView->clear();
  this->m_chartView->initializeAxis(2);

  if (this->m_checkForceZero->isChecked())
    RegressionString.asprintf("y = %0.2fx", this->m_hwm->slope());
  else
    RegressionString.asprintf("y = %0.2fx + %0.2f", this->m_hwm->slope(),
                             this->m_hwm->intercept());

  CorrelationString.asprintf("%0.2f", this->m_hwm->r2());
  StandardDeviationString.asprintf("%0.2f", this->m_hwm->standardDeviation());

  RegressionTitle = this->m_plotTitleBox->text();
  YLabel = this->m_modeledAxisLabelBox->text();
  XLabel = this->m_measuredAxisLabelBox->text();
  displayBoundingLines = this->m_checkStdDevLines->isChecked();

  HWMColor = Colors::styleSheetToColor(this->m_buttonColorHwm->styleSheet());
  One2OneColor =
      Colors::styleSheetToColor(this->m_buttonColor121Line->styleSheet());
  BoundColor =
      Colors::styleSheetToColor(this->m_buttonColorBoundingLine->styleSheet());
  RegColor =
      Colors::styleSheetToColor(this->m_buttonColorRegLine->styleSheet());

  boundValue =
      this->m_boundingLineValue->value() * this->m_hwm->standardDeviation();

  doColorDots = this->m_checkColorDots->isChecked();

  QVector<QScatterSeries *> scatterSeries;
  scatterSeries.resize(8);
  for (int i = 0; i < 8; i++)
    scatterSeries[i] = new QScatterSeries(this->m_chartView->chart());

  if (doColorDots) {
    dotColors.resize(8);
    dotColors[0].setNamedColor("#B8B8B8");
    dotColors[1].setNamedColor("#FF00FF");
    dotColors[2].setNamedColor("#8282CD");
    dotColors[3].setNamedColor("#006600");
    dotColors[4].setNamedColor("#00CC66");
    dotColors[5].setNamedColor("#CCCC00");
    dotColors[6].setNamedColor("#FF9933");
    dotColors[7].setNamedColor("#FF0000");
    for (int i = 0; i < 8; i++) scatterSeries[i]->setColor(dotColors[i]);
  } else {
    dotColors.resize(8);
    dotColors[0].setNamedColor("#B8B8B8");
    dotColors[1] = HWMColor;
    dotColors[2] = HWMColor;
    dotColors[3] = HWMColor;
    dotColors[4] = HWMColor;
    dotColors[5] = HWMColor;
    dotColors[6] = HWMColor;
    dotColors[7] = HWMColor;
    for (int i = 0; i < 8; i++) scatterSeries[i]->setColor(dotColors[i]);
  }

  this->m_chartView->xAxis()->setTitleText(XLabel);
  this->m_chartView->yAxis()->setTitleText(YLabel);

  min = std::numeric_limits<double>::max();
  max = -std::numeric_limits<double>::max();

  for (int i = 0; i < this->m_hwm->n(); ++i) {
    int classification = this->classifyHWM(this->m_hwm->hwm(i)->modeledError());

    if (this->m_hwm->hwm(i)->modeledElevation() > -900)
      scatterSeries[classification]->append(
          QPointF(this->m_hwm->hwm(i)->observedElevation(),
                  this->m_hwm->hwm(i)->modeledElevation()));
    else
      scatterSeries[classification]->append(
          QPointF(this->m_hwm->hwm(i)->observedElevation(),
                  this->m_hwm->hwm(i)->observedElevation()));

    if (this->m_hwm->hwm(i)->modeledElevation() > max &&
        this->m_hwm->hwm(i)->modeledElevation() > -900)
      max = this->m_hwm->hwm(i)->modeledElevation();
    if (this->m_hwm->hwm(i)->modeledElevation() < min &&
        this->m_hwm->hwm(i)->modeledElevation() > -900)
      min = this->m_hwm->hwm(i)->modeledElevation();
    if (this->m_hwm->hwm(i)->observedElevation() > max &&
        this->m_hwm->hwm(i)->observedElevation() > -900)
      max = this->m_hwm->hwm(i)->observedElevation();
    if (this->m_hwm->hwm(i)->observedElevation() < min &&
        this->m_hwm->hwm(i)->observedElevation() > -900)
      min = this->m_hwm->hwm(i)->observedElevation();
  }

  this->m_chartView->setAxisLimits(min, max, min, max);

  for (int i = 0; i < 8; i++) {
    this->m_chartView->chart()->addSeries(scatterSeries[i]);
    scatterSeries[i]->attachAxis(this->m_chartView->xAxis());
    scatterSeries[i]->attachAxis(this->m_chartView->yAxis());
    scatterSeries[i]->setName(tr("High Water Marks"));
  }

  //...Don't display all the HWM series
  for (int i = 0; i < 8; i++)
    this->m_chartView->chart()->legend()->markers().at(i)->setVisible(false);

  //...1:1 line
  QLineSeries *One2OneLine = new QLineSeries(this->m_chartView->chart());
  One2OneLine->append(-1000, -1000);
  One2OneLine->append(1000, 1000);
  One2OneLine->setPen(QPen(QBrush(One2OneColor), 3));
  this->m_chartView->chart()->addSeries(One2OneLine);
  One2OneLine->attachAxis(this->m_chartView->xAxis());
  One2OneLine->attachAxis(this->m_chartView->yAxis());
  One2OneLine->setName("1:1 Line");

  //...Regression Line
  QLineSeries *RegressionLine = new QLineSeries(this->m_chartView->chart());
  RegressionLine->append(
      -1000, this->m_hwm->slope() * -1000 + this->m_hwm->intercept());
  RegressionLine->append(
      1000, this->m_hwm->slope() * 1000 + this->m_hwm->intercept());
  RegressionLine->setPen(QPen(QBrush(RegColor), 3));
  this->m_chartView->chart()->addSeries(RegressionLine);
  RegressionLine->attachAxis(this->m_chartView->xAxis());
  RegressionLine->attachAxis(this->m_chartView->yAxis());
  RegressionLine->setName(tr("Regression Line"));

  //...Standard Deviation Lines
  if (displayBoundingLines) {
    //...Upper Bound Line
    QLineSeries *UpperBoundLine = new QLineSeries(this->m_chartView->chart());
    UpperBoundLine->append(-1000, -1000 + boundValue);
    UpperBoundLine->append(1000, 1000 + boundValue);
    UpperBoundLine->setPen(QPen(QBrush(BoundColor), 3));
    this->m_chartView->chart()->addSeries(UpperBoundLine);
    UpperBoundLine->attachAxis(this->m_chartView->xAxis());
    UpperBoundLine->attachAxis(this->m_chartView->yAxis());
    UpperBoundLine->setName(tr("Standard Deviation Interval"));

    //...Lower Bound Line
    QLineSeries *LowerBoundLine = new QLineSeries(this->m_chartView->chart());
    LowerBoundLine->append(-1000, -1000 - boundValue);
    LowerBoundLine->append(1000, 1000 - boundValue);
    LowerBoundLine->setPen(QPen(QBrush(BoundColor), 3));
    this->m_chartView->chart()->addSeries(LowerBoundLine);
    LowerBoundLine->attachAxis(this->m_chartView->xAxis());
    LowerBoundLine->attachAxis(this->m_chartView->yAxis());
    LowerBoundLine->setName(tr("Standard Deviation Interval"));

    this->m_chartView->chart()->legend()->markers().at(10)->setVisible(false);
    this->m_chartView->chart()->legend()->markers().at(11)->setVisible(false);
  }

  this->m_chartView->chart()->setTitle(RegressionTitle);
  this->m_chartView->initializeAxisLimits();
  this->m_chartView->initializeLegendMarkers();
  this->m_chartView->setStatusBar(this->m_statusBar);
  this->m_chartView->setInfoString(RegressionString, CorrelationString,
                                   StandardDeviationString);

  this->m_chartView->setInfoItem(
      new QGraphicsTextItem(this->m_chartView->chart()));
  this->m_chartView->infoItem()->setZValue(10);
  this->m_chartView->infoItem()->setHtml(this->m_chartView->infoString());
  this->m_chartView->infoItem()->setPos(90, 70);
  this->m_chartView->setInfoRectItem(
      new QGraphicsRectItem(this->m_chartView->chart()));
  this->m_chartView->infoRectItem()->setZValue(9);
  this->m_chartView->infoRectItem()->setPos(90, 70);
  this->m_chartView->infoRectItem()->setRect(
      this->m_chartView->infoItem()->boundingRect());
  this->m_chartView->infoRectItem()->setBrush(Qt::white);

  return 0;
}

int Hwm::processHWMData() {
  int ierr = this->readHWMData();
  if (ierr != 0) {
    this->m_errorString = tr("Could not process the high water mark file.");
    return -1;
  }

  // Sanity check on classes
  if (this->m_checkUserClasses->isChecked()) {
    double c0 = this->m_classes[0];
    double c1 = this->m_classes[1];
    double c2 = this->m_classes[2];
    double c3 = this->m_classes[3];
    double c4 = this->m_classes[4];
    double c5 = this->m_classes[5];
    double c6 = this->m_classes[6];

    if (c1 <= c0) {
      this->m_errorString = tr("Your classifications are invalid.");
      return -1;
    } else if (c2 <= c1) {
      this->m_errorString = tr("Your classifications are invalid.");
      return -1;
    } else if (c3 <= c2) {
      this->m_errorString = tr("Your classifications are invalid.");
      return -1;
    } else if (c4 <= c3) {
      this->m_errorString = tr("Your classifications are invalid.");
      return -1;
    } else if (c5 <= c4) {
      this->m_errorString = tr("Your classifications are invalid.");
      return -1;
    } else if (c6 <= c5) {
      this->m_errorString = tr("Your classifications are invalid.");
      return -1;
    }
  }

  ierr = this->plotHWMMap();
  if (ierr != 0) return ierr;

  ierr = this->plotRegression();
  if (ierr != 0) return ierr;

  return 0;
}

int Hwm::readHWMData() {
  this->m_hwm->setFilename(this->m_fileBox->text());
  this->m_hwm->setRegressionThroughZero(this->m_checkForceZero);
  return this->m_hwm->read();
}

int Hwm::saveHWMMap(QString outputFile, QString filter) {
  if (filter == "PDF (*.pdf)") {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize::Letter);
    printer.setResolution(400);
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outputFile);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.begin(&printer);

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
    QFile output(outputFile);
    QSize imageSize(this->m_quickMap->size().width(),
                    this->m_quickMap->size().height());

    QImage pixmap(imageSize, QImage::Format_ARGB32);
    pixmap.fill(Qt::white);
    QPainter imagePainter(&pixmap);
    imagePainter.setRenderHints(QPainter::Antialiasing |
                                QPainter::TextAntialiasing |
                                QPainter::SmoothPixmapTransform);
    this->m_quickMap->render(&imagePainter, QPoint(0, 0));

    output.open(QIODevice::WriteOnly);
    pixmap.save(&output, "JPG", 100);
  }

  return 0;
}

int Hwm::saveRegressionPlot(QString outputFile, QString filter) {
  if (filter == "PDF (*.pdf)") {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize::Letter);
    printer.setResolution(400);
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(outputFile);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.begin(&printer);

    this->m_chartView->render(&painter);

    painter.end();
  } else if (filter == "JPG (*.jpg *.jpeg)") {
    QFile output(outputFile);
    QSize imageSize(this->m_chartView->size().width(),
                    this->m_chartView->size().height());
    QRect chartRect(0, 0, this->m_chartView->size().width(),
                    this->m_chartView->size().height());

    QImage pixmap(imageSize, QImage::Format_ARGB32);
    pixmap.fill(Qt::white);
    QPainter imagePainter(&pixmap);
    imagePainter.setRenderHints(QPainter::Antialiasing |
                                QPainter::TextAntialiasing |
                                QPainter::SmoothPixmapTransform);
    this->m_chartView->render(&imagePainter, chartRect);

    output.open(QIODevice::WriteOnly);
    pixmap.save(&output, "JPG", 100);
  }

  return 0;
}
