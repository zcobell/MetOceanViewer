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

mov_QChartView::mov_QChartView(QWidget *parent) : QChartView(parent)
{
    setDragMode(QChartView::NoDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setMouseTracking(true);

    m_chart  = NULL;
    m_coordX = NULL;
    m_coordY = NULL;
    m_coordZ = NULL;
    m_style  = 0;

}

void mov_QChartView::resizeEvent(QResizeEvent *event)
{
    if (scene()) {
        scene()->setSceneRect(QRect(QPoint(0, 0), event->size()));
        if(m_chart)
        {
            m_chart->resize(event->size());
            m_coordX->setPos(m_chart->size().width()/2 - 100, m_chart->size().height() - 20);
            m_coordY->setPos(m_chart->size().width()/2 + 20, m_chart->size().height() - 20);
            if(m_style==2)
                m_coordZ->setPos(m_chart->size().width()/2 + 120, m_chart->size().height() - 20);
        }
    }
    QChartView::resizeEvent(event);
}

void mov_QChartView::mouseMoveEvent(QMouseEvent *event)
{
    QString dateString;
    QDateTime date;
    qreal     x,y;

    if(m_coordX)
    {
        x = m_chart->mapToValue(event->pos()).x();
        y = m_chart->mapToValue(event->pos()).y();
        if(x<x_axis_max && x>x_axis_min && y<y_axis_max && y>y_axis_min)
        {
            if(m_style==1)
            {
                date = QDateTime::fromMSecsSinceEpoch(x);
                date.setTimeSpec(Qt::UTC);
                dateString = QString("Date: ")+date.toString("MM/dd/yyyy hh:mm AP");
                m_coordX->setText(dateString);
                m_coordY->setText(QString("Value: %1").arg(y));
            }
            else if(m_style==2)
            {
                m_coordX->setText(QString("Measured: %1").arg(x));
                m_coordY->setText(QString("Modeled: %1").arg(y));
                m_coordZ->setText(QString("Diff: %1").arg(y-x));
            }
        }
        else
        {
            m_coordX->setText("");
            m_coordY->setText("");
            if(m_style==2)
                m_coordZ->setText("");
        }
    }
    QChartView::mouseMoveEvent(event);
}

