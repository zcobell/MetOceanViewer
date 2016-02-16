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
#include "usgs.h"
#include <QtGui/QResizeEvent>
#include <QtWidgets/QGraphicsScene>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtWidgets/QGraphicsTextItem>
#include <QtGui/QMouseEvent>

usgs::usgs(QWebEngineView *inMap,
           mov_QChartView *inChart, QRadioButton *inDailyButton,
           QRadioButton *inHistoricButton, QRadioButton *inInstantButton,
           QComboBox *inProductBox, QDateEdit *inStartDateEdit,
           QDateEdit *inEndDateEdit, QStatusBar *instatusBar, QObject *parent) : QObject(parent)
{

    //...Initialize variables
    this->USGSDataReady = false;
    this->USGSBeenPlotted = false;
    this->CurrentUSGSStationName = "none";
    this->USGSMarkerID = "none";
    this->ProductIndex = 0;
    this->ProductName = "none";

    //...Assign object pointers
    this->map = inMap;
    this->chart = inChart;
    this->dailyButton = inDailyButton;
    this->historicButton = inHistoricButton;
    this->instantButton = inInstantButton;
    this->productBox = inProductBox;
    this->startDateEdit = inStartDateEdit;
    this->endDateEdit = inEndDateEdit;
    this->statusBar = instatusBar;

}

usgs::~usgs()
{

}

void usgs::handleLegendMarkerClicked()
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
