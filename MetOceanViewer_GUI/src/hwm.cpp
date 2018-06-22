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
  this->m_regCorrelation = 0.0;
  this->m_regLineIntercept = 0.0;
  this->m_regLineSlope = 0.0;
  this->m_regStdDev = 0.0;
  this->m_stationModel = stationModel;
  this->m_classes = inClassValues;
}

QString Hwm::getErrorString() { return this->m_errorString; }

int Hwm::classifyHWM(double diff) {
  for (int i = 0; i < 7; i++) {
    if (diff < this->m_classes[i]) return i;
  }
  return 7;
}

int Hwm::computeLinearRegression() {
  try {
    double SumXY = 0;
    double SumX2 = 0;
    double SumY2 = 0;
    double SumY = 0;
    double SumX = 0;
    double M = 0;
    double B = 0;
    double N = static_cast<double>(this->m_highWaterMarks.size());
    double NDry = 0;
    double SumErr = 0;
    for (int i = 0; i < N; i++) {
      // We ditch points that didn't wet since they
      // skew calculation
      if (this->m_highWaterMarks[i].modeled > -9999) {
        SumX = SumX + (this->m_highWaterMarks[i].measured);
        SumY = SumY + (this->m_highWaterMarks[i].modeled);
        SumXY = SumXY + (this->m_highWaterMarks[i].measured *
                         this->m_highWaterMarks[i].modeled);
        SumX2 = SumX2 + (this->m_highWaterMarks[i].measured *
                         this->m_highWaterMarks[i].measured);
        SumY2 = SumY2 + (this->m_highWaterMarks[i].modeled *
                         this->m_highWaterMarks[i].modeled);
        SumErr = SumErr + this->m_highWaterMarks[i].error;
      } else {
        NDry = NDry + 1;
      }
    }

    // Number of points that we'll end up using
    double N2 = N - NDry;

    double R2, MeanErr, StdDev;

    // Calculate the slope (M) and Correllation (R2)
    if (this->m_checkForceZero->isChecked()) {
      // Slope
      M = SumXY / SumX2;

      // Forced through zero
      B = 0;

      // Average Y
      double YBar = SumY / N2;

      // Calculate Total Sum of Squares
      double SSTOT = 0.0;
      for (int i = 0; i < N; i++) {
        // We ditch points that didn't wet since they
        // skew calculation
        if (this->m_highWaterMarks[i].modeled > -9999) {
          SSTOT = SSTOT + qPow((this->m_highWaterMarks[i].modeled - YBar), 2.0);
        }
      }

      // Sum of square errors
      double SSE = SumY2 - M * M * SumX2;

      // R2
      R2 = 1 - (SSE / SSTOT);
    } else {
      // Slope
      M = (N2 * SumXY - SumX * SumY) / (N2 * SumX2 - (SumX * SumX));

      // Intercept
      B = ((SumY * SumX2) - (SumX * SumXY)) / (N2 * SumX2 - (SumX * SumX));

      // R2 calculation
      R2 = qPow(
          ((N2 * SumXY - (SumX * SumY)) /
           sqrt((N2 * SumX2 - (SumX * SumX)) * (N2 * SumY2 - (SumY * SumY)))),
          2.0);
    }

    // Calculate Standard Deviation
    MeanErr = SumErr / N2;
    SumErr = 0;
    for (int i = 0; i < N; i++)
      if (this->m_highWaterMarks[i].modeled > -9999)
        SumErr = SumErr + qPow(this->m_highWaterMarks[i].error - MeanErr, 2.0);

    StdDev = qSqrt(SumErr / N2);

    this->m_regLineSlope = M;
    this->m_regLineIntercept = B;
    this->m_regCorrelation = R2;
    this->m_regStdDev = StdDev;

  } catch (...) {
    return 1;
  }

  return 0;
}

int Hwm::plotHWMMap() {
  QString unitString;

  this->m_stationModel->clear();

  if (this->m_comboUnits->currentIndex() == 1)
    unitString = "m";
  else
    unitString = "ft";

  for (int i = 0; i < this->m_highWaterMarks.length(); i++) {
    int classification;
    if (this->m_highWaterMarks[i].modeled < -9999)
      classification = -1;
    else
      classification = this->classifyHWM(this->m_highWaterMarks[i].error);

    this->m_stationModel->addMarker(
        Station(QGeoCoordinate(this->m_highWaterMarks[i].lat,
                               this->m_highWaterMarks[i].lon),
                QString::number(i), "hwm", this->m_highWaterMarks[i].measured,
                this->m_highWaterMarks[i].modeled, classification));
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
    RegressionString.sprintf("y = %0.2fx", this->m_regLineSlope);
  else
    RegressionString.sprintf("y = %0.2fx + %0.2f", this->m_regLineSlope,
                             this->m_regLineIntercept);

  CorrelationString.sprintf("%0.2f", this->m_regCorrelation);
  StandardDeviationString.sprintf("%0.2f", this->m_regStdDev);

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

  boundValue = this->m_boundingLineValue->value() * this->m_regStdDev;

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

  min = DBL_MAX;
  max = DBL_MIN;

  for (int i = 0; i < this->m_highWaterMarks.length(); i++) {
    int classification = this->classifyHWM(this->m_highWaterMarks[i].error);

    if (this->m_highWaterMarks[i].modeled > -900)
      scatterSeries[classification]->append(
          QPointF(this->m_highWaterMarks[i].measured,
                  this->m_highWaterMarks[i].modeled));
    else
      scatterSeries[classification]->append(
          QPointF(this->m_highWaterMarks[i].measured,
                  this->m_highWaterMarks[i].measured));

    if (this->m_highWaterMarks[i].modeled > max &&
        this->m_highWaterMarks[i].modeled > -900)
      max = this->m_highWaterMarks[i].modeled;
    if (this->m_highWaterMarks[i].modeled < min &&
        this->m_highWaterMarks[i].modeled > -900)
      min = this->m_highWaterMarks[i].modeled;
    if (this->m_highWaterMarks[i].measured > max &&
        this->m_highWaterMarks[i].measured > -900)
      max = this->m_highWaterMarks[i].measured;
    if (this->m_highWaterMarks[i].measured < min &&
        this->m_highWaterMarks[i].measured > -900)
      min = this->m_highWaterMarks[i].measured;
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
      -1000, this->m_regLineSlope * -1000 + this->m_regLineIntercept);
  RegressionLine->append(
      1000, this->m_regLineSlope * 1000 + this->m_regLineIntercept);
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
    this->m_errorString = tr("Could not read the high water mark file.");
    return -1;
  }

  ierr = this->computeLinearRegression();
  if (ierr != 0) {
    this->m_errorString = tr("Could not calculate the regression function.");
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
  QString Line;
  QStringList List;

  QFile MyFile(this->m_fileBox->text());

  // Check if we can open the file
  if (!MyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    this->m_errorString = MyFile.errorString();
    return -1;
  }

  try {
    int nLines = 0;
    while (!MyFile.atEnd()) {
      Line = MyFile.readLine();
      nLines = nLines + 1;
    }
    MyFile.close();
    MyFile.open(QIODevice::ReadOnly | QIODevice::Text);
    this->m_highWaterMarks.resize(nLines);
    int i = 0;
    while (!MyFile.atEnd()) {
      Line = MyFile.readLine().simplified();
      List = Line.split(",");
      this->m_highWaterMarks[i].lon = List.value(0).toDouble();
      this->m_highWaterMarks[i].lat = List.value(1).toDouble();
      this->m_highWaterMarks[i].bathy = List.value(2).toDouble();
      this->m_highWaterMarks[i].measured = List.value(3).toDouble();
      this->m_highWaterMarks[i].modeled = List.value(4).toDouble();
      this->m_highWaterMarks[i].error = List.value(5).toDouble();
      i = i + 1;
    }
    return 0;
  } catch (...) {
    this->m_errorString = tr("Unexpected error reading file.");
    return 1;
  }
}

int Hwm::saveHWMMap(QString outputFile, QString filter) {
  if (filter == "PDF (*.pdf)") {
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPrinter::Letter);
    printer.setResolution(400);
    printer.setOrientation(QPrinter::Landscape);
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
    printer.setPageSize(QPrinter::Letter);
    printer.setResolution(400);
    printer.setOrientation(QPrinter::Landscape);
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
