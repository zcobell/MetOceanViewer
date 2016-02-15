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

#ifndef MOV_QCHARTVIEW_H
#define MOV_QCHARTVIEW_H
#include <QChartView>
#include <QtCharts/QChartGlobal>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
class QMouseEvent;
class QResizeEvent;
QT_END_NAMESPACE

QT_CHARTS_BEGIN_NAMESPACE
class QChart;
QT_CHARTS_END_NAMESPACE

QT_CHARTS_USE_NAMESPACE

class mov_QChartView: public QChartView
{
    Q_OBJECT

public:
    mov_QChartView(QWidget *parent = 0);

    QGraphicsSimpleTextItem *m_coordX;
    QGraphicsSimpleTextItem *m_coordY;
    QGraphicsSimpleTextItem *m_coordZ;
    QChart  *m_chart;

    qreal   x_axis_min,x_axis_max;
    qreal   y_axis_min,y_axis_max;
    int     m_style;

protected:
    void resizeEvent(QResizeEvent *event);
    void mouseMoveEvent(QMouseEvent *event);

};

#endif //MOVQCHARTVIEW_H
