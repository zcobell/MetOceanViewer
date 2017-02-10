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
#include <QtGui/QResizeEvent>
#include <QtWidgets/QGraphicsScene>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtWidgets/QGraphicsTextItem>
#include <QLegendMarker>
#include <QtGui/QMouseEvent>
#include <QDateTime>
#include "movQChartView.h"


MovQChartView::MovQChartView(QWidget *parent) : QChartView(parent)
{
    setDragMode(QChartView::NoDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setMouseTracking(true);

    this->m_chart     = NULL;
    this->m_coord     = NULL;
    this->m_info      = NULL;
    this->m_statusBar = NULL;
    this->m_style     = 0;
    this->x_axis_max  = 0.0;
    this->x_axis_min  = 0.0;
    this->y_axis_max  = 0.0;
    this->y_axis_min  = 0.0;
    this->current_x_axis_max = 0.0;
    this->current_x_axis_min = 0.0;
    this->current_y_axis_max = 0.0;
    this->current_y_axis_min = 0.0;
    this->setRubberBand(QChartView::RectangleRubberBand);
}


void MovQChartView::clear()
{
    this->m_legendNames.clear();
    this->m_series.clear();
    this->m_kdtree.clear();
    return;
}


void MovQChartView::addSeries(QLineSeries *series, QString name)
{
    this->m_series.resize(this->m_series.length()+1);
    this->m_legendNames.resize(this->m_legendNames.length()+1);
    this->m_kdtree.resize(this->m_kdtree.length()+1);

    this->m_series[this->m_series.length()-1] = series;
    this->m_legendNames[this->m_legendNames.length()-1] = name;

    this->m_kdtree[this->m_kdtree.length()-1] = new qKdtree2(this);
    QList<QPointF> points = this->m_series[this->m_series.length()-1]->points();
    this->m_kdtree[this->m_kdtree.length()-1]->build(points);
    return;
}

void MovQChartView::resizeEvent(QResizeEvent *event)
{
    if (scene())
    {

        scene()->setSceneRect(QRect(QPoint(0, 0), event->size()));
        if(m_chart)
        {
            m_chart->resize(event->size());
            m_coord->setPos(m_chart->size().width()/2 - 100, m_chart->size().height() - 20);
        }
        if(m_info)
        {
            m_info->setPos(10,m_chart->size().height() - 50);
        }
    }
    QChartView::resizeEvent(event);
    return;
}

void MovQChartView::mouseMoveEvent(QMouseEvent *event)
{
    QString   dateString;
    QDateTime date;
    qreal     x,y;
    int       i_min = 0;
    qreal     y_dum = 0.0;

    if(this->m_coord)
    {
        x = this->m_chart->mapToValue(event->pos()).x();
        y = this->m_chart->mapToValue(event->pos()).y();

        if(x<this->current_x_axis_max && x>this->current_x_axis_min && y<this->current_y_axis_max && y>this->current_y_axis_min)
        {
            if(this->m_style==1)
            {
                for(int i=0;i<this->m_series.length();i++)
                {
                    this->m_kdtree.at(i)->findNearest(x,y_dum,i_min);
                    this->chart()->series().at(i)->setName(this->m_legendNames.at(i)+": "+
                                                           QString::number(this->m_series[i]->points().at(i_min).y()));
                }
                date = QDateTime::fromMSecsSinceEpoch(this->m_series[0]->at(i_min).x());
                date.setTimeSpec(Qt::UTC);
                dateString = QString("Date: ")+date.toString("MM/dd/yyyy hh:mm AP");
                this->m_coord->setText(dateString);
            }
            else if(this->m_style==2)
                this->m_coord->setText(tr("Measured: %1     Modeled: %2     Diff: %3").arg(x).arg(y).arg(y-x));

            if(this->m_statusBar)
                this->m_statusBar->showMessage(tr("Left click and drag to zoom in, Right click to zoom out, Double click to reset zoom"));
        }
        else
        {
            this->m_coord->setText("");
            if(this->m_statusBar)
                this->m_statusBar->clearMessage();
            for(int i=0;i<this->m_series.length();i++)
                this->chart()->series().at(i)->setName(this->m_legendNames.at(i));
        }
    }

    QChartView::mouseMoveEvent(event);
    return;
}

void MovQChartView::mouseReleaseEvent(QMouseEvent *event)
{
    QChartView::mouseReleaseEvent(event);
    if(this->m_chart)
        this->resetAxisLimits();
    return;

}

void MovQChartView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(this->m_chart)
        this->resetZoom();
    QChartView::mouseDoubleClickEvent(event);
    return;
}

void MovQChartView::wheelEvent(QWheelEvent *event)
{
    if(this->m_chart==NULL)
        return;

    if(event->delta()>0)
        this->m_chart->zoomIn();
    else if(event->delta()<0)
        this->m_chart->zoomOut();

    QChartView::wheelEvent(event);
    return;
}

void MovQChartView::resetZoom()
{
    if(this->m_chart)
    {
        this->chart()->zoomReset();
        this->resetAxisLimits();
    }
    return;
}

void MovQChartView::resetAxisLimits()
{
    if(this->m_chart)
    {
        qreal x1,x2,y1,y2;

        QRectF box = this->m_chart->plotArea();
        x1 = m_chart->mapToValue(box.bottomLeft()).x();
        x2 = m_chart->mapToValue(box.topRight()).x();
        y1 = m_chart->mapToValue(box.bottomLeft()).y();
        y2 = m_chart->mapToValue(box.topRight()).y();

        current_x_axis_min = x1;
        current_x_axis_max = x2;
        current_y_axis_min = y1;
        current_y_axis_max = y2;
    }

    return;
}

void MovQChartView::initializeAxisLimits()
{
    qreal x1,x2,y1,y2;
    QRectF box = this->m_chart->plotArea();
    x1 = m_chart->mapToValue(box.bottomLeft()).x();
    x2 = m_chart->mapToValue(box.topRight()).x();
    y1 = m_chart->mapToValue(box.bottomLeft()).y();
    y2 = m_chart->mapToValue(box.topRight()).y();
    x_axis_min = x1;
    y_axis_min = y1;
    x_axis_max = x2;
    y_axis_max = y2;
    current_x_axis_min = x_axis_min;
    current_x_axis_max = x_axis_max;
    current_y_axis_min = y_axis_min;
    current_y_axis_max = y_axis_max;
    current_x_axis_max = x_axis_max;
    current_y_axis_max = y_axis_max;
    current_x_axis_min = x_axis_min;
    current_y_axis_min = y_axis_min;
    return;
}

void MovQChartView::setStatusBar(QStatusBar *inStatusBar)
{
    this->m_statusBar = inStatusBar;
    return;
}

void MovQChartView::handleLegendMarkerClicked()
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
            break;
    }
}
