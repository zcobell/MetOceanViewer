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
#ifndef MOV_HWM_H
#define MOV_HWM_H

#include <QChartView>
#include <QFile>
#include <QObject>
#include <QWebEngineView>
#include <QtCharts>
#include <QtMath>
#include <QtWidgets>

#include "movQChartView.h"

class MovHwm : public QObject {
  Q_OBJECT
public:
  explicit MovHwm(QLineEdit *inFilebox, QCheckBox *inManualCheck,
                  QComboBox *inUnitCombobox, QCheckBox *inForceThroughZero,
                  QCheckBox *inUpperLowerLines, QCheckBox *inColorHWMDots,
                  QPushButton *inHWMColor, QPushButton *inButton121LineColor,
                  QPushButton *inButtonBoundingLineColor,
                  QPushButton *inButtonRegLineColor,
                  QLineEdit *inModeledAxisLabel, QLineEdit *inMeasuredAxisLabel,
                  QLineEdit *inPlotTitle, QSpinBox *inBoundingLinesValue,
                  QWebEngineView *inMap, MovQChartView *inChartView,
                  QStatusBar *inStatusBar, QVector<double> &inClassValues,
                  QObject *parent = 0);

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
  double regLineSlope, regLineIntercept;
  double regCorrelation, regStdDev;
  QString hwmErrorString;
  QVector<double> classes;
  QVector<hwm_data> highWaterMarks;

  //...Private Functions
  int readHWMData();
  int computeLinearRegression();
  int classifyHWM(double diff);
  int plotHWMMap();
  int plotRegression();

  //...Pointers to widgets
  QLineEdit *fileBox, *modeledAxisLabelBox;
  QLineEdit *measuredAxisLabelBox, *plotTitleBox;
  QPushButton *buttonHWMColor, *button121LineColor;
  QPushButton *buttonBoundingLinecolor, *buttonRegLineColor;
  QCheckBox *manualClassificationCheckbox, *forceThroughZeroCheckbox;
  QCheckBox *upperLowerLinesCheckbox, *colorHWMDotsCheckbox;
  QSpinBox *boundingLinesValue;
  QComboBox *unitComboBox;
  MovQChartView *chartView;
  QWebEngineView *map;
  QChart *thisChart;
  QStatusBar *statusBar;
};

#endif // MOV_HWM_H
