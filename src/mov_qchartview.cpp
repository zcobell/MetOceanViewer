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
#include <mov_qchartview.h>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QGraphicsScene>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtWidgets/QGraphicsTextItem>
#include <QtGui/QMouseEvent>
#include <QDateTime>
#include <QDebug>

mov_QChartView::mov_QChartView(QWidget *parent) : QChartView(parent)
{
    setDragMode(QChartView::NoDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setMouseTracking(true);

    m_chart     = NULL;
    m_coord     = NULL;
    m_info      = NULL;
    m_statusBar = NULL;
    m_style     = 0;
    this->setRubberBand(QChartView::RectangleRubberBand);
}

void mov_QChartView::resizeEvent(QResizeEvent *event)
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

void mov_QChartView::mouseMoveEvent(QMouseEvent *event)
{
    QString   dateString;
    QDateTime date;
    qreal     x,y;

    if(this->m_coord)
    {
        x = this->m_chart->mapToValue(event->pos()).x();
        y = this->m_chart->mapToValue(event->pos()).y();
        if(x<this->current_x_axis_max && x>this->current_x_axis_min && y<this->current_y_axis_max && y>this->current_y_axis_min)
        {
            if(this->m_style==1)
            {
                date = QDateTime::fromMSecsSinceEpoch(x);
                date.setTimeSpec(Qt::UTC);
                dateString = QString("Date: ")+date.toString("MM/dd/yyyy hh:mm AP");
                this->m_coord->setText(dateString+QString("     Value: %1").arg(y));
            }
            else if(this->m_style==2)
                this->m_coord->setText(QString("Measured: %1     Modeled: %2     Diff: %3").arg(x).arg(y).arg(y-x));

            if(this->m_statusBar)
                this->m_statusBar->showMessage("Left click and drag to zoom in, Right click to zoom out, Double click to reset zoom");
        }
        else
        {
            this->m_coord->setText("");
            if(this->m_statusBar)
                this->m_statusBar->clearMessage();
        }
    }

    QChartView::mouseMoveEvent(event);
    return;
}

void mov_QChartView::mouseReleaseEvent(QMouseEvent *event)
{
    QChartView::mouseReleaseEvent(event);
    if(this->m_chart)
        this->resetAxisLimits();
    return;

}

void mov_QChartView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(this->m_chart)
        this->resetZoom();
    QChartView::mouseDoubleClickEvent(event);
    return;
}

void mov_QChartView::wheelEvent(QWheelEvent *event)
{
    if(event->delta()>0)
        this->m_chart->zoomIn();
    else if(event->delta()<0)
        this->m_chart->zoomOut();

    QChartView::wheelEvent(event);
    return;
}

void mov_QChartView::resetZoom()
{
    if(this->m_chart)
    {
        this->chart()->zoomReset();
        this->resetAxisLimits();
    }
    return;
}

void mov_QChartView::resetAxisLimits()
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

void mov_QChartView::initializeAxisLimits()
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

void mov_QChartView::setStatusBar(QStatusBar *inStatusBar)
{
    this->m_statusBar = inStatusBar;
    return;
}
