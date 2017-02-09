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
#include "movHwm.h"
#include "movGeneric.h"
#include "movColors.h"
#include <QPrinter>
#include <float.h>

MovHwm::MovHwm(QLineEdit *inFilebox, QCheckBox *inManualCheck, QComboBox *inUnitCombobox,
         QCheckBox *inForceThroughZero, QCheckBox *inUpperLowerLines, QCheckBox *inColorHWMDots,
         QPushButton *inHWMColor, QPushButton *inButton121LineColor,
         QPushButton *inButtonBoundingLineColor, QPushButton *inButtonRegLineColor,
         QLineEdit *inModeledAxisLabel, QLineEdit *inMeasuredAxisLabel,
         QLineEdit *inPlotTitle, QSpinBox *inBoundingLinesValue, QWebEngineView *inMap, MovQChartView *inChartView,
         QStatusBar *inStatusBar, QVector<double> &inClassValues, QObject *parent) : QObject(parent)
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
    this->statusBar = inStatusBar;
    this->regCorrelation = 0.0;
    this->regLineIntercept = 0.0;
    this->regLineSlope = 0.0;
    this->regStdDev = 0.0;
    this->thisChart = NULL;

    this->classes.resize(inClassValues.length());
    for(int i=0;i<this->classes.length();i++)
        classes[i] = inClassValues[i];

}

QString MovHwm::getErrorString()
{
    return this->hwmErrorString;
}

int MovHwm::classifyHWM(double diff)
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

int MovHwm::computeLinearRegression()
{
    double SumXY, SumX2, SumX, SumY, SumY2, N, NDry, N2, SSE, SSTOT, YBar, SumErr, MeanErr;
    double M,B,R2,StdDev;
    int i;

    bool ForceThroughZero = this->forceThroughZeroCheckbox->isChecked();

    SumXY  = 0;
    SumX2  = 0;
    SumY2  = 0;
    SumY   = 0;
    SumX   = 0;
    M      = 0;
    B      = 0;
    N      = static_cast<double>(this->highWaterMarks.size());
    NDry   = 0;
    SSE    = 0;
    SSTOT  = 0;
    SumErr = 0;

    try
    {
        for(i = 0; i<N; i++)
        {
            //We ditch points that didn't wet since they
            //skew calculation
            if(this->highWaterMarks[i].modeled>-9999)
            {
                SumX   = SumX   + (this->highWaterMarks[i].measured);
                SumY   = SumY   + (this->highWaterMarks[i].modeled);
                SumXY  = SumXY  + (this->highWaterMarks[i].measured*this->highWaterMarks[i].modeled);
                SumX2  = SumX2  + (this->highWaterMarks[i].measured*this->highWaterMarks[i].measured);
                SumY2  = SumY2  + (this->highWaterMarks[i].modeled*this->highWaterMarks[i].modeled);
                SumErr = SumErr +  this->highWaterMarks[i].error;
            }
            else
                NDry = NDry + 1;
        }

        //Number of points that we'll end up using
        N2 = N - NDry;

        //Calculate the slope (M) and Correllation (R2)
        if(ForceThroughZero)
        {
            //Slope
            M   = SumXY / SumX2;

            //Forced through zero
            B   = 0;

            //Average Y
            YBar = SumY / N2;

            //Calculate Total Sum of Squares
            for(i = 0; i<N; i++)
            {
                //We ditch points that didn't wet since they
                //skew calculation
                if(this->highWaterMarks[i].modeled>-9999)
                {
                    SSTOT = SSTOT + qPow((this->highWaterMarks[i].modeled - YBar),2.0);
                }
            }

            //Sum of square errors
            SSE = SumY2 - M*M*SumX2;

            //R2
            R2  = 1 - ( SSE / SSTOT );
        }
        else
        {
            //Slope
            M  = (N2*SumXY - SumX*SumY) / (N2*SumX2-(SumX*SumX));

            //Intercept
            B  = ((SumY*SumX2)-(SumX*SumXY))/(N2*SumX2-(SumX*SumX));

            //R2 calculation
            R2 = qPow(((N2*SumXY - (SumX*SumY)) /
                   sqrt( (N2*SumX2 - (SumX*SumX))*(N2*SumY2-(SumY*SumY)) )),2.0);
        }

        //Calculate Standard Deviation
        MeanErr = SumErr / N2;
        SumErr = 0;
        for( i = 0; i<N; i++)
            if(this->highWaterMarks[i].modeled>-9999)
                SumErr = SumErr + qPow(this->highWaterMarks[i].error - MeanErr,2.0);

        StdDev = qSqrt(SumErr / N2);


    }
    catch(...)
    {
        return 1;
    }

    this->regLineSlope = M;
    this->regLineIntercept = B;
    this->regCorrelation = R2;
    this->regStdDev = StdDev;

    return 0;

}

int MovHwm::plotHWMMap()
{

    QString MeasuredString,ModeledString,Marker,MyClassList;
    QString unitString;
    double x,y,measurement,modeled,error,MaximumValue;
    int i,classification,units;

    units = this->unitComboBox->currentIndex();
    if(units==1)
        unitString = "'m'";
    else
        unitString = "'ft'";

    //Plot the high water mark map
    MeasuredString = "";
    ModeledString = "";
    MaximumValue = 0;

    //Make sure we are fresh for if this is a second round of plotting
    this->map->page()->runJavaScript("clearMarkers()");

    //Give the browsers a chance to catch up to us
    MovGeneric::delay(1);

    for(i=0;i<this->highWaterMarks.length();i++)
    {
        x = this->highWaterMarks[i].lon;
        y = this->highWaterMarks[i].lat;
        measurement = this->highWaterMarks[i].measured;
        modeled = this->highWaterMarks[i].modeled;
        error = this->highWaterMarks[i].error;

        if(modeled < -9999)
            classification = -1;
        else
            classification = this->classifyHWM(error);

        if(measurement > MaximumValue)
            MaximumValue = measurement + 1;
        else if(modeled > MaximumValue)
            MaximumValue = modeled + 1;

        Marker = "addHWM("+QString::number(x)+","+QString::number(y)+
                ","+QString::number(i)+","+QString::number(modeled)+","+QString::number(measurement)+
                ","+QString::number(error)+","+QString::number(classification)+","+unitString+
                ")";
        this->map->page()->runJavaScript(Marker);
        if(i==0)
        {
            ModeledString = QString::number(modeled);
            MeasuredString = QString::number(measurement);
        }
        else
        {
            ModeledString = ModeledString+":"+QString::number(modeled);
            MeasuredString = MeasuredString+":"+QString::number(measurement);
        }
    }

    MyClassList = "addLegend("+unitString+",'"+QString::number(classes[0],'f',2)+":"+QString::number(classes[1],'f',2)+":"+
            QString::number(classes[2],'f',2)+":"+QString::number(classes[3],'f',2)+":"+
            QString::number(classes[4],'f',2)+":"+QString::number(classes[5],'f',2)+":"+
            QString::number(classes[6],'f',2)+"')";
    this->map->page()->runJavaScript(MyClassList);

    return 0;
}

int MovHwm::plotRegression()
{

    QString RegressionTitle,XLabel,YLabel;
    QString RegressionString,CorrelationString,StandardDeviationString;
    QColor HWMColor,One2OneColor,BoundColor,RegColor;
    int i,numSD,classification;
    double boundValue,confidence,min,max;
    bool displayBoundingLines,doColorDots;
    QVector<QColor> dotColors;

    this->thisChart = new QChart();
    this->chartView->m_chart = this->thisChart;

    if(this->forceThroughZeroCheckbox->isChecked())
        RegressionString = QString("y = %1x").arg(qRound(this->regLineSlope*100.0)/100.0);
    else
        RegressionString = QString("y = %1x + %2").arg(qRound(this->regLineSlope*100.0)/100.0).arg(qRound(this->regLineIntercept*100.0)/100.0);

    CorrelationString = QString::number(qRound(this->regCorrelation*100.0)/100.0);
    StandardDeviationString = QString::number(qRound(this->regStdDev*100.0)/100.0);

    RegressionTitle = this->plotTitleBox->text();
    YLabel          = this->modeledAxisLabelBox->text();
    XLabel          = this->measuredAxisLabelBox->text();
    displayBoundingLines = this->upperLowerLinesCheckbox->isChecked();

    HWMColor = MovColors::styleSheetToColor(this->buttonHWMColor->styleSheet());
    One2OneColor = MovColors::styleSheetToColor(this->button121LineColor->styleSheet());
    BoundColor = MovColors::styleSheetToColor(this->buttonBoundingLinecolor->styleSheet());
    RegColor = MovColors::styleSheetToColor(this->buttonRegLineColor->styleSheet());

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
        scatterSeries[i] = new QScatterSeries(this);

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


    QValueAxis *axisX = new QValueAxis(this);
    QValueAxis *axisY = new QValueAxis(this);
    axisX->setTitleText(XLabel);
    axisY->setTitleText(YLabel);
    this->thisChart->addAxis(axisX, Qt::AlignBottom);
    this->thisChart->addAxis(axisY, Qt::AlignLeft);

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
    axisX->setTickCount(10);
    axisY->setTickCount(10);

    axisX->applyNiceNumbers();
    axisY->applyNiceNumbers();

    for(i=0;i<8;i++)
    {
        this->thisChart->addSeries(scatterSeries[i]);
        scatterSeries[i]->attachAxis(axisX);
        scatterSeries[i]->attachAxis(axisY);
        scatterSeries[i]->setName("High Water Marks");
    }

    //...Don't display all the HWM series
    for(i=0;i<8;i++)
        this->thisChart->legend()->markers().at(i)->setVisible(false);

    //...1:1 line
    QLineSeries *One2OneLine = new QLineSeries(this);
    One2OneLine->append(-1000,-1000);
    One2OneLine->append(1000,1000);
    One2OneLine->setPen(QPen(QBrush(One2OneColor),3));
    this->thisChart->addSeries(One2OneLine);
    One2OneLine->attachAxis(axisX);
    One2OneLine->attachAxis(axisY);
    One2OneLine->setName("1:1 Line");

    //...Regression Line
    QLineSeries *RegressionLine = new QLineSeries(this);
    RegressionLine->append(-1000,this->regLineSlope*-1000+this->regLineIntercept);
    RegressionLine->append(1000,this->regLineSlope*1000+this->regLineIntercept);
    RegressionLine->setPen(QPen(QBrush(RegColor),3));
    this->thisChart->addSeries(RegressionLine);
    RegressionLine->attachAxis(axisX);
    RegressionLine->attachAxis(axisY);
    RegressionLine->setName("Regression Line");

    //...Standard Deviation Lines
    if(displayBoundingLines)
    {
        //...Upper Bound Line
        QLineSeries *UpperBoundLine = new QLineSeries(this);
        UpperBoundLine->append(-1000,-1000+boundValue);
        UpperBoundLine->append(1000,1000+boundValue);
        UpperBoundLine->setPen(QPen(QBrush(BoundColor),3));
        this->thisChart->addSeries(UpperBoundLine);
        UpperBoundLine->attachAxis(axisX);
        UpperBoundLine->attachAxis(axisY);
        UpperBoundLine->setName("Standard Deviation Interval");

        //...Lower Bound Line
        QLineSeries *LowerBoundLine = new QLineSeries(this);
        LowerBoundLine->append(-1000,-1000-boundValue);
        LowerBoundLine->append(1000,1000-boundValue);
        LowerBoundLine->setPen(QPen(QBrush(BoundColor),3));
        this->thisChart->addSeries(LowerBoundLine);
        LowerBoundLine->attachAxis(axisX);
        LowerBoundLine->attachAxis(axisY);
        LowerBoundLine->setName("Standard Deviation Interval");

        this->thisChart->legend()->markers().at(10)->setVisible(false);
        this->thisChart->legend()->markers().at(11)->setVisible(false);
    }

    axisX->setGridLineColor(QColor(200,200,200));
    axisY->setGridLineColor(QColor(200,200,200));
    axisY->setShadesPen(Qt::NoPen);
    axisY->setShadesBrush(QBrush(QColor(240,240,240)));
    axisY->setShadesVisible(true);

    axisX->setTitleFont(QFont("Helvetica",10,QFont::Bold));
    axisY->setTitleFont(QFont("Helvetica",10,QFont::Bold));

    for(i=0;i<this->thisChart->legend()->markers().length();i++)
        this->thisChart->legend()->markers().at(i)->setFont(QFont("Helvetica",10,QFont::Bold));

    this->thisChart->legend()->setAlignment(Qt::AlignBottom);
    this->thisChart->setTitle(RegressionTitle);
    this->thisChart->setTitleFont(QFont("Helvetica",14,QFont::Bold));
    this->thisChart->setAnimationOptions(QChart::SeriesAnimations);
    this->chartView->setRenderHint(QPainter::Antialiasing);
    this->chartView->setChart(this->thisChart);

    this->chartView->m_style = 2;
    this->chartView->m_coord = new QGraphicsSimpleTextItem(this->thisChart);
    this->chartView->m_coord->setPos(this->chartView->size().width()/2 - 100, this->chartView->size().height() - 20);

    this->chartView->m_info = new QGraphicsTextItem(this->thisChart);
    this->chartView->m_infoString = "<table><tr><td align=\"right\"><b> Regression Line: </b></td><td>"+RegressionString+"</td></tr>"+
                                     "<tr><td align=\"right\"><b> Correlation (R&sup2;): </b></td><td>"+CorrelationString+"</td></tr>"+
                                     "<tr><td align=\"right\"><b> Standard Deviation: </b></td><td>"+StandardDeviationString+"</td></tr></table>";
    this->chartView->m_info->setHtml(this->chartView->m_infoString);
    this->chartView->m_info->setPos(10,this->chartView->m_chart->size().height()-50);
    this->chartView->initializeAxisLimits();
    this->chartView->setStatusBar(this->statusBar);

    foreach (QLegendMarker* marker, this->thisChart->legend()->markers())
    {
        // Disconnect possible existing connection to avoid multiple connections
        QObject::disconnect(marker, SIGNAL(clicked()), this->chartView, SLOT(handleLegendMarkerClicked()));
        QObject::connect(marker, SIGNAL(clicked()), this->chartView, SLOT(handleLegendMarkerClicked()));
    }

    return 0;
}


int MovHwm::processHWMData()
{
    QString unitString;
    double c0,c1,c2,c3,c4,c5,c6;
    int unit, ierr;
    bool ThroughZero;

    ThroughZero = this->forceThroughZeroCheckbox->isChecked();

    ierr = this->readHWMData();
    if(ierr!=0)
    {
        this->hwmErrorString = "Could not read the high water mark file.";
        return -1;
    }

    ierr = this->computeLinearRegression();
    if(ierr!=0)
    {
        this->hwmErrorString = "Could not calculate the regression function.";
        return -1;
    }

    unit = this->unitComboBox->currentIndex();
    if(unit==0)
        unitString = "'ft'";
    else
        unitString = "'m'";

    this->map->page()->runJavaScript("clearMarkers()");

    //Sanity check on classes
    if(this->manualClassificationCheckbox->isChecked())
    {
        c0 = this->classes[0];
        c1 = this->classes[1];
        c2 = this->classes[2];
        c3 = this->classes[3];
        c4 = this->classes[4];
        c5 = this->classes[5];
        c6 = this->classes[6];

        if(c1<=c0)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c2<=c1)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c3<=c2)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c4<=c3)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c5<=c4)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
        else if(c6<=c5)
        {
            this->hwmErrorString = "Your classifications are invalid.";
            return -1;
        }
    }

    ierr = this->plotHWMMap();

    ierr = this->plotRegression();

    return 0;
}


int MovHwm::readHWMData()
{
    QString Line;
    QStringList List;
    int nLines;

    QFile MyFile(this->fileBox->text());

    //Check if we can open the file
    if(!MyFile.open(QIODevice::ReadOnly|QIODevice::Text))
    {
        this->hwmErrorString = MyFile.errorString();
        return -1;
    }

    try
    {
        nLines = 0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine();
            nLines = nLines + 1;
        }
        MyFile.close();
        MyFile.open(QIODevice::ReadOnly|QIODevice::Text);
        this->highWaterMarks.resize(nLines);
        int i=0;
        while(!MyFile.atEnd())
        {
            Line = MyFile.readLine().simplified();
            List = Line.split(",");
            this->highWaterMarks[i].lon = List.value(0).toDouble();
            this->highWaterMarks[i].lat = List.value(1).toDouble();
            this->highWaterMarks[i].bathy = List.value(2).toDouble();
            this->highWaterMarks[i].measured = List.value(3).toDouble();
            this->highWaterMarks[i].modeled = List.value(4).toDouble();
            this->highWaterMarks[i].error = List.value(5).toDouble();
            i = i + 1;
        }
        return 0;
    }
    catch(...)
    {
        this->hwmErrorString = "Unexpected error reading file.";
        return 1;
    }

}


int MovHwm::saveHWMMap(QString outputFile, QString filter)
{
    if(filter=="PDF (*.pdf)")
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageSize(QPrinter::Letter);
        printer.setResolution(400);
        printer.setOrientation(QPrinter::Landscape);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(outputFile);

        QPainter painter(&printer);
        painter.setRenderHint(QPainter::Antialiasing,true);
        painter.begin(&printer);

        QPixmap renderedMap = this->map->grab();
        QPixmap mapScaled = renderedMap.scaledToWidth(printer.width());
        if(mapScaled.height()>printer.height())
            mapScaled = renderedMap.scaledToHeight(printer.height());
        int cw = (printer.width()-mapScaled.width())/2;
        int ch = (printer.height()-mapScaled.height())/2;
        painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

        painter.end();
    }
    else if(filter=="JPG (*.jpg *.jpeg)")
    {
        QFile output(outputFile);
        QSize imageSize(this->map->size().width(),this->map->size().height());

        QImage pixmap(imageSize, QImage::Format_ARGB32);
        pixmap.fill(Qt::white);
        QPainter imagePainter(&pixmap);
        imagePainter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        this->map->render(&imagePainter,QPoint(0,0));

        output.open(QIODevice::WriteOnly);
        pixmap.save(&output,"JPG",100);
    }

    return 0;
}


int MovHwm::saveRegressionPlot(QString outputFile, QString filter)
{

    if(filter == "PDF (*.pdf)")
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageSize(QPrinter::Letter);
        printer.setResolution(400);
        printer.setOrientation(QPrinter::Landscape);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(outputFile);

        QPainter painter(&printer);
        painter.setRenderHint(QPainter::Antialiasing,true);
        painter.begin(&printer);

        this->chartView->render(&painter);

        painter.end();
    }
    else if(filter == "JPG (*.jpg *.jpeg)")
    {
        QFile output(outputFile);
        QSize imageSize(this->chartView->size().width(),this->chartView->size().height());
        QRect chartRect(0,0,this->chartView->size().width(),this->chartView->size().height());

        QImage pixmap(imageSize, QImage::Format_ARGB32);
        pixmap.fill(Qt::white);
        QPainter imagePainter(&pixmap);
        imagePainter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        this->chartView->render(&imagePainter,chartRect);

        output.open(QIODevice::WriteOnly);
        pixmap.save(&output,"JPG",100);
    }

    return 0;
}
