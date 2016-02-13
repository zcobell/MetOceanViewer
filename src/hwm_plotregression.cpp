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
#include <hwm.h>
#include <general_functions.h>

int hwm::plotRegression()
{

    QString RegressionTitle,XLabel,YLabel;
    QColor HWMColor,One2OneColor,BoundColor,RegColor;
    int i,numSD,classification,nFrac;
    double boundValue,confidence,min,max;
    bool displayBoundingLines,doColorDots;
    QVector<double> labels;
    QVector<QColor> dotColors;

    QChart *thisChart = new QChart();

    RegressionTitle = this->plotTitleBox->text();
    YLabel          = this->modeledAxisLabelBox->text();
    XLabel          = this->measuredAxisLabelBox->text();
    displayBoundingLines = this->upperLowerLinesCheckbox->isChecked();

    HWMColor = styleSheetToColor(this->buttonHWMColor->styleSheet());
    One2OneColor = styleSheetToColor(this->button121LineColor->styleSheet());
    BoundColor = styleSheetToColor(this->buttonBoundingLinecolor->styleSheet());
    RegColor = styleSheetToColor(this->buttonRegLineColor->styleSheet());

    boundValue = this->boundingLinesValue->value()*this->regStdDev;
    numSD      = this->boundingLinesValue->value();

    doColorDots = this->colorHWMDotsCheckbox->isChecked();

    if(numSD==1)
        confidence=68.0;
    else if(numSD==2)
        confidence=95.0;
    else if(numSD==3)
        confidence=99.7;
    else
        confidence=0.0;

    QVector<QScatterSeries*> scatterSeries;
    scatterSeries.resize(8);
    for(i=0;i<8;i++)
        scatterSeries[i] = new QScatterSeries;

    if(doColorDots)
    {
        dotColors.resize(8);
        dotColors[0].setNamedColor("#B8B8B8");
        dotColors[1].setNamedColor("#FF00FF");
        dotColors[2].setNamedColor("#8282CD");
        dotColors[3].setNamedColor("#006600");
        dotColors[4].setNamedColor("#00CC66");
        dotColors[5].setNamedColor("#CCCC00");
        dotColors[6].setNamedColor("#FF9933");
        dotColors[7].setNamedColor("#FF0000");
        for(i=0;i<8;i++)
            scatterSeries[i]->setColor(dotColors[i]);
    }
    else
    {
        dotColors.resize(8);
        dotColors[0].setNamedColor("#B8B8B8");
        dotColors[1] = HWMColor;
        dotColors[2] = HWMColor;
        dotColors[3] = HWMColor;
        dotColors[4] = HWMColor;
        dotColors[5] = HWMColor;
        dotColors[6] = HWMColor;
        dotColors[7] = HWMColor;
        for(i=0;i<8;i++)
            scatterSeries[i]->setColor(dotColors[i]);
    }


    QValueAxis *axisX = new QValueAxis;
    QValueAxis *axisY = new QValueAxis;
    axisX->setTitleText(XLabel);
    axisY->setTitleText(YLabel);
    thisChart->addAxis(axisX, Qt::AlignBottom);
    thisChart->addAxis(axisY, Qt::AlignLeft);

    min = DBL_MAX;
    max = DBL_MIN;

    for(i=0;i<this->highWaterMarks.length();i++)
    {
        classification = this->classifyHWM(this->highWaterMarks[i].error);

        if(this->highWaterMarks[i].modeled>-900)
            scatterSeries[classification-1]->append(QPointF(this->highWaterMarks[i].measured,this->highWaterMarks[i].modeled));
        else
            scatterSeries[classification-1]->append(QPointF(this->highWaterMarks[i].measured,this->highWaterMarks[i].measured));

        if(this->highWaterMarks[i].modeled>max && this->highWaterMarks[i].modeled>-900)
            max = this->highWaterMarks[i].modeled;
        if(this->highWaterMarks[i].modeled<min && this->highWaterMarks[i].modeled>-900)
            min = this->highWaterMarks[i].modeled;
        if(this->highWaterMarks[i].measured>max && this->highWaterMarks[i].measured>-900)
            max = this->highWaterMarks[i].measured;
        if(this->highWaterMarks[i].measured<min && this->highWaterMarks[i].measured>-900)
            min = this->highWaterMarks[i].measured;
    }

    axisY->setMin(min);
    axisY->setMax(max);
    axisX->setMin(min);
    axisX->setMax(max);

    axisX->applyNiceNumbers();
    axisY->applyNiceNumbers();

    for(i=0;i<8;i++)
    {
        thisChart->addSeries(scatterSeries[i]);
        scatterSeries[i]->attachAxis(axisX);
        scatterSeries[i]->attachAxis(axisY);
        scatterSeries[i]->setName("High Water Marks");
    }

    //...Don't display all the HWM series
    for(i=0;i<8;i++)
        thisChart->legend()->markers().at(i)->setVisible(false);

    //...1:1 line
    QLineSeries *One2OneLine = new QLineSeries;
    One2OneLine->append(-1000,-1000);
    One2OneLine->append(1000,1000);
    One2OneLine->setPen(QPen(QBrush(One2OneColor),3));
    thisChart->addSeries(One2OneLine);
    One2OneLine->attachAxis(axisX);
    One2OneLine->attachAxis(axisY);
    One2OneLine->setName("1:1 Line");

    //...Regression Line
    QLineSeries *RegressionLine = new QLineSeries;
    RegressionLine->append(-1000,this->regLineSlope*-1000+this->regLineIntercept);
    RegressionLine->append(1000,this->regLineSlope*1000+this->regLineIntercept);
    RegressionLine->setPen(QPen(QBrush(RegColor),3));
    thisChart->addSeries(RegressionLine);
    RegressionLine->attachAxis(axisX);
    RegressionLine->attachAxis(axisY);
    RegressionLine->setName("Regression Line");

    //...Standard Deviation Lines
    if(displayBoundingLines)
    {
        //...Upper Bound Line
        QLineSeries *UpperBoundLine = new QLineSeries;
        UpperBoundLine->append(-1000,-1000+boundValue);
        UpperBoundLine->append(1000,1000+boundValue);
        UpperBoundLine->setPen(QPen(QBrush(BoundColor),3));
        thisChart->addSeries(UpperBoundLine);
        UpperBoundLine->attachAxis(axisX);
        UpperBoundLine->attachAxis(axisY);
        UpperBoundLine->setName("Standard Deviation Interval");

        //...Lower Bound Line
        QLineSeries *LowerBoundLine = new QLineSeries;
        LowerBoundLine->append(-1000,-1000-boundValue);
        LowerBoundLine->append(1000,1000-boundValue);
        LowerBoundLine->setPen(QPen(QBrush(BoundColor),3));
        thisChart->addSeries(LowerBoundLine);
        LowerBoundLine->attachAxis(axisX);
        LowerBoundLine->attachAxis(axisY);
        LowerBoundLine->setName("Standard Deviation Interval");

        thisChart->legend()->markers().at(10)->setVisible(false);
        thisChart->legend()->markers().at(11)->setVisible(false);
    }

    thisChart->legend()->setAlignment(Qt::AlignBottom);
    thisChart->setTitle(RegressionTitle);
    thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->chartView->setRenderHint(QPainter::Antialiasing);
    this->chartView->setChart(thisChart);

    return 0;
}
