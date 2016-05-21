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

#ifndef MOV_QCHARTVIEW_H
#define MOV_QCHARTVIEW_H
#include <QtWidgets>
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

    void initializeAxisLimits();
    void resetZoom();
    void setStatusBar(QStatusBar *inStatusBar);

    QGraphicsSimpleTextItem *m_coord;
    QGraphicsTextItem       *m_info;
    QChart                  *m_chart;
    int                      m_style;
    QString                  m_infoString;

protected:
    void resizeEvent(QResizeEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void wheelEvent(QWheelEvent *event);

private:
    void        resetAxisLimits();
    qreal       x_axis_min,x_axis_max;
    qreal       y_axis_min,y_axis_max;
    qreal       current_x_axis_min,current_x_axis_max;
    qreal       current_y_axis_min,current_y_axis_max;
    QStatusBar *m_statusBar;

public slots:
    void handleLegendMarkerClicked();

};

#endif //MOVQCHARTVIEW_H
