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
#ifndef HWM_H
#define HWM_H

#include <QChartView>
#include <QFile>
#include <QObject>
#include <QQuickWidget>
#include <QtCharts>
#include <QtMath>
#include <QtWidgets>
#include "chartview.h"
#include "stationmodel.h"

class Hwm : public QObject {
  Q_OBJECT
 public:
  explicit Hwm(QLineEdit *inFilebox, QCheckBox *inManualCheck,
               QComboBox *inUnitCombobox, QCheckBox *inForceThroughZero,
               QCheckBox *inUpperLowerLines, QCheckBox *inColorHWMDots,
               QPushButton *inHWMColor, QPushButton *inButton121LineColor,
               QPushButton *inButtonBoundingLineColor,
               QPushButton *inButtonRegLineColor, QLineEdit *inModeledAxisLabel,
               QLineEdit *inMeasuredAxisLabel, QLineEdit *inPlotTitle,
               QSpinBox *inBoundingLinesValue, QQuickWidget *inMap,
               ChartView *inChartView, QStatusBar *inStatusBar,
               QVector<double> &inClassValues, StationModel *stationModel,
               QObject *parent = nullptr);

  //...Public Functions
  int processHWMData();
  int saveHWMMap(QString outputFile, QString filter);
  int saveRegressionPlot(QString outputFile, QString filter);
  QString getErrorString();

 private:
  //...High water mark structure
  struct hwm_data {
    double lat;
    double lon;
    double bathy;
    double measured;
    double modeled;
    double error;
  };

  //...Private Variables
  double m_regLineSlope, m_regLineIntercept;
  double m_regCorrelation, m_regStdDev;
  QString m_errorString;
  QVector<double> m_classes;
  QVector<hwm_data> m_highWaterMarks;

  //...Private Functions
  int readHWMData();
  int computeLinearRegression();
  int classifyHWM(double diff);
  int plotHWMMap();
  int plotRegression();

  //...Pointers to widgets
  QLineEdit *m_fileBox, *m_modeledAxisLabelBox;
  QLineEdit *m_measuredAxisLabelBox, *m_plotTitleBox;
  QPushButton *m_buttonColorHwm, *m_buttonColor121Line;
  QPushButton *m_buttonColorBoundingLine, *m_buttonColorRegLine;
  QCheckBox *m_checkUserClasses, *m_checkForceZero;
  QCheckBox *m_checkStdDevLines, *m_checkColorDots;
  QSpinBox *m_boundingLineValue;
  QComboBox *m_comboUnits;
  ChartView *m_chartView;
  QQuickWidget *m_quickMap;
  QStatusBar *m_statusBar;
  StationModel *m_stationModel;

};

#endif  // HWM_H
