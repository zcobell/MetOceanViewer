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
#include <user_timeseries.h>
#include <general_functions.h>
#include <mov_flags.h>

user_timeseries::user_timeseries(QTableWidget *inTable, QCheckBox *inXAxisCheck,
                                 QCheckBox *inYAxisCheck, QDateEdit *inStartDate,
                                 QDateEdit *inEndDate, QDoubleSpinBox *inYMinEdit,
                                 QDoubleSpinBox *inYMaxEdit, QLineEdit *inPlotTitle,
                                 QLineEdit *inXLabelEdit, QLineEdit *inYLabelEdit,
                                 QWebEngineView *inMap, mov_QChartView *inChart,
                                 QStatusBar *inStatusBar, QVector<QColor> inRandomColorList,
                                 QObject *parent) : QObject(parent)
{
    table      = inTable;
    xAxisCheck = inXAxisCheck;
    yAxisCheck = inYAxisCheck;
    startDate  = inStartDate;
    endDate    = inEndDate;
    yMaxEdit   = inYMaxEdit;
    yMinEdit   = inYMinEdit;
    plotTitle  = inPlotTitle;
    xLabelEdit = inXLabelEdit;
    yLabelEdit = inYLabelEdit;
    map        = inMap;
    chart      = inChart;
    statusBar  = inStatusBar;
    randomColorList = inRandomColorList;
}

user_timeseries::~user_timeseries()
{

}

int user_timeseries::getDataBounds(double &ymin, double &ymax, QDateTime &minDate, QDateTime &maxDate, QVector<double> timeAddList)
{
    int i,j;
    double unitConversion,addY;

    ymin = DBL_MAX;
    ymax = DBL_MIN;
    minDate = QDateTime(QDate(3000,1,1),QTime(0,0,0));
    maxDate = QDateTime(QDate(1500,1,1),QTime(0,0,0));

    for(i=0;i<fileDataUnique.length();i++)
    {
        unitConversion = table->item(i,3)->text().toDouble();
        addY = table->item(i,5)->text().toDouble();
        for(j=0;j<fileDataUnique[i].station[markerID].NumSnaps;j++)
        {
            if(fileDataUnique[i].station[markerID].data[j]*unitConversion+addY<ymin && fileDataUnique[i].station[markerID].data[j]!=MOV_NULL_TS)
                ymin = fileDataUnique[i].station[markerID].data[j]*unitConversion+addY;
            if(fileDataUnique[i].station[markerID].data[j]*unitConversion+addY>ymax && fileDataUnique[i].station[markerID].data[j]!=MOV_NULL_TS)
                ymax = fileDataUnique[i].station[markerID].data[j]*unitConversion+addY;
            if(fileDataUnique[i].station[markerID].date[j].addSecs(timeAddList[i]*3600.0)<minDate)
                minDate = fileDataUnique[i].station[markerID].date[j].addSecs(timeAddList[i]*3600.0);
            if(fileDataUnique[i].station[markerID].date[j].addSecs(timeAddList[i]*3600.0)>maxDate)
                maxDate = fileDataUnique[i].station[markerID].date[j].addSecs(timeAddList[i]*3600.0);
        }
    }
    return 0;
}

void user_timeseries::handleLegendMarkerClicked()
{
    QLegendMarker* marker = qobject_cast<QLegendMarker*> (sender());

    Q_ASSERT(marker);

    switch (marker->type())
    {
        case QLegendMarker::LegendMarkerTypeXY:
        {
            // Toggle visibility of series
            marker->series()->setVisible(!marker->series()->isVisible());

            // Turn legend marker back to visible, since hiding series also hides the marker
            // and we don't want it to happen now.
            marker->setVisible(true);

            // Dim the marker, if series is not visible
            qreal alpha = 1.0;

            if (!marker->series()->isVisible())
                alpha = 0.5;

            QColor color;
            QBrush brush = marker->labelBrush();
            color = brush.color();
            color.setAlphaF(alpha);
            brush.setColor(color);
            marker->setLabelBrush(brush);

            brush = marker->brush();
            color = brush.color();
            color.setAlphaF(alpha);
            brush.setColor(color);
            marker->setBrush(brush);

            QPen pen = marker->pen();
            color = pen.color();
            color.setAlphaF(alpha);
            pen.setColor(color);
            marker->setPen(pen);

            break;
        }
        default:
        {
            qDebug() << "Unknown marker type";
            break;
        }
    }
}

int user_timeseries::saveImage(QString filename, QString filter)
{
    if(filter == "PDF (*.pdf)")
    {
        QPrinter printer(QPrinter::HighResolution);
        printer.setPageSize(QPrinter::Letter);
        printer.setResolution(400);
        printer.setOrientation(QPrinter::Landscape);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(filename);

        QPainter painter(&printer);
        painter.setRenderHint(QPainter::Antialiasing,true);
        painter.begin(&printer);

        //...Page 1 - Chart
        this->chart->render(&painter);

        //...Page 2 - Map
        printer.newPage();
        QPixmap renderedMap = this->map->grab();
        QPixmap mapScaled = renderedMap.scaledToWidth(printer.width());
        if(mapScaled.height()>printer.height())
            mapScaled = renderedMap.scaledToHeight(printer.height());
        int cw = (printer.width()-mapScaled.width())/2;
        int ch = (printer.height()-mapScaled.height())/2;
        painter.drawPixmap(cw,ch,mapScaled.width(),mapScaled.height(),mapScaled);

        painter.end();
    }
    else if(filter == "JPG (*.jpg *.jpeg)")
    {
        QFile outputFile(filename);
        QSize imageSize(this->map->size().width()+this->chart->size().width(),this->map->size().height());
        QRect chartRect(this->map->size().width(),0,this->chart->size().width(),this->chart->size().height());

        QImage pixmap(imageSize, QImage::Format_ARGB32);
        pixmap.fill(Qt::white);
        QPainter imagePainter(&pixmap);
        imagePainter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        this->map->render(&imagePainter,QPoint(0,0));
        this->chart->render(&imagePainter,chartRect);

        outputFile.open(QIODevice::WriteOnly);
        pixmap.save(&outputFile,"JPG",100);
    }

    return 0;
}
