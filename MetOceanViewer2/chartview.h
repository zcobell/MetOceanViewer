/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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

#ifndef CHARTVIEW_H
#define CHARTVIEW_H
#include <QChartView>
#include <QDateTimeAxis>
#include <QLineF>
#include <QLineSeries>
#include <QValueAxis>
#include <QtCharts/QChartGlobal>
#include <QtWidgets>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
class QMouseEvent;
class QResizeEvent;
QT_END_NAMESPACE

QT_CHARTS_BEGIN_NAMESPACE
class QChart;
QT_CHARTS_END_NAMESPACE

QT_CHARTS_USE_NAMESPACE

class ChartView : public QChartView {
  Q_OBJECT

 public:
  ChartView(QWidget *parent = nullptr);

  ~ChartView();

  void initializeAxisLimits();
  void resetZoom();
  void setStatusBar(QStatusBar *inStatusBar);
  void addSeries(QLineSeries *series, QString name);
  void setDisplayValues(bool value);
  void rebuild();
  void clear();

  QGraphicsSimpleTextItem *coord() const;

  int style() const;
  void setStyle(int style);

  QString infoString() const;
  void setInfoString(QString infoString);
  void setInfoString(QString regressionString, QString correlationString,
                     QString standardDeviationString);

  QDateTimeAxis *dateAxis() const;
  QValueAxis *xAxis() const;
  QValueAxis *yAxis() const;

  void setDateFormat(QDateTime start, QDateTime end);

  void initializeLegendMarkers();
  void initializeAxis(int style = 1);
  void setAxisLimits(QDateTime startDate, QDateTime endDate, double ymin,
                     double ymax);
  void setAxisLimits(double xmin, double xmax, double ymin, double ymax);

  QGraphicsRectItem *infoRectItem() const;
  void setInfoRectItem(QGraphicsRectItem *infoRectItem);

  QGraphicsTextItem *infoItem() const;
  void setInfoItem(QGraphicsTextItem *infoItem);

 protected:
  void resizeEvent(QResizeEvent *event);
  void mouseMoveEvent(QMouseEvent *event);
  void mouseDoubleClickEvent(QMouseEvent *event);
  void mouseReleaseEvent(QMouseEvent *event);
  void wheelEvent(QWheelEvent *event);

 private:
  void resetAxisLimits();

  QPointF dateDisplayPosition();

  static bool pointXLessThan(const QPointF &p1, const QPointF &p2);

  qreal x_axis_min, x_axis_max;
  qreal y_axis_min, y_axis_max;
  qreal current_x_axis_min, current_x_axis_max;
  qreal current_y_axis_min, current_y_axis_max;
  QStatusBar *m_statusBar;
  QVector<QString> m_legendNames;
  QVector<QLineSeries *> m_series;
  QLineF m_yTraceLine;
  QLineF m_xTraceLine;
  QGraphicsItem *m_yTraceLinePtr;
  QGraphicsItem *m_xTraceLinePtr;
  bool m_displayValues;

  QDateTimeAxis *m_dateAxis;
  QValueAxis *m_xAxis, *m_yAxis;
  QGraphicsSimpleTextItem *m_coord;
  int m_style;
  QString m_infoString;

  QGraphicsRectItem *m_infoRectItem;
  QGraphicsTextItem *m_infoItem;

  bool getNearestPointToCursor(qreal cursorXPosition, int seriesIndex, qreal &x,
                               qreal &y);
  bool isOnPlot(qreal x, qreal y);
  void addTraceLines(QMouseEvent *event);
  void addXTraceLine(QMouseEvent *event);
  void addXYTraceLine(QMouseEvent *event);
  void removeTraceLines();

  void addLineValuesToLegend(qreal x);

  void addChartPositionToLegend(qreal x, qreal y);

  void makeDynamicLegendLabels(qreal x, qreal y);

  void displayInstructionsOnStatusBar();

  void resetPlotLegend();

 public slots:
  void handleLegendMarkerClicked();
};

#endif  // CHARTVIEW_H
