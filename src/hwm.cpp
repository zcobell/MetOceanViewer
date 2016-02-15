//-------------------------------GPL-------------------------------------//
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
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//
#include "hwm.h"

hwm::hwm(QLineEdit *inFilebox, QCheckBox *inManualCheck, QComboBox *inUnitCombobox,
         QCheckBox *inForceThroughZero, QCheckBox *inUpperLowerLines, QCheckBox *inColorHWMDots,
         QPushButton *inHWMColor, QPushButton *inButton121LineColor,
         QPushButton *inButtonBoundingLineColor, QPushButton *inButtonRegLineColor,
         QLineEdit *inModeledAxisLabel, QLineEdit *inMeasuredAxisLabel,
         QLineEdit *inPlotTitle, QSpinBox *inBoundingLinesValue, QWebEngineView *inMap, mov_QChartView *inChartView,
         QVector<double> &inClassValues, QObject *parent) : QObject(parent)
{

    this->fileBox = inFilebox;
    this->manualClassificationCheckbox = inManualCheck;
    this->unitComboBox = inUnitCombobox;
    this->forceThroughZeroCheckbox = inForceThroughZero;
    this->buttonHWMColor = inHWMColor;
    this->button121LineColor = inButton121LineColor;
    this->buttonBoundingLinecolor = inButtonBoundingLineColor;
    this->buttonRegLineColor = inButtonRegLineColor;
    this->modeledAxisLabelBox = inModeledAxisLabel;
    this->measuredAxisLabelBox = inMeasuredAxisLabel;
    this->upperLowerLinesCheckbox = inUpperLowerLines;
    this->boundingLinesValue = inBoundingLinesValue;
    this->colorHWMDotsCheckbox = inColorHWMDots;
    this->plotTitleBox = inPlotTitle;
    this->chartView = inChartView;
    this->map = inMap;

    this->classes.resize(inClassValues.length());
    for(int i=0;i<this->classes.length();i++)
        classes[i] = inClassValues[i];

}

QString hwm::getErrorString()
{
    return this->hwmErrorString;
}

int hwm::classifyHWM(double diff)
{
    int color;
    if(diff<this->classes[0])
        color=1;
    else if(diff<this->classes[1])
        color=2;
    else if(diff<this->classes[2])
        color=3;
    else if(diff<this->classes[3])
        color=4;
    else if(diff<this->classes[4])
        color=5;
    else if(diff<this->classes[5])
        color=6;
    else if(diff<this->classes[6])
        color=7;
    else
        color=8;
    return color;
}
