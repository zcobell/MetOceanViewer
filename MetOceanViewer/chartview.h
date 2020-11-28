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
  explicit ChartView(QWidget *parent = nullptr);

  ~ChartView() override;

  void initializeAxisLimits();
  void resetZoom();
  void setStatusBar(QStatusBar *inStatusBar);
  void addSeries(QLineSeries *series, const QString &name);
  void setDisplayValues(bool value);
  void rebuild();
  void clear();

  [[nodiscard]] QGraphicsSimpleTextItem *coord() const;

  [[nodiscard]] int chartStyle() const;
  void setStyle(int style);

  [[nodiscard]] QString infoString() const;
  void setInfoString(QString infoString);
  void setInfoString(const QString &regressionString,
                     const QString &correlationString,
                     const QString &standardDeviationString);

  [[nodiscard]] QDateTimeAxis *dateAxis() const;
  [[nodiscard]] QValueAxis *xAxis() const;
  [[nodiscard]] QValueAxis *yAxis() const;

  void initializeLegendMarkers();
  void initializeAxis(int style = 1);
  void setAxisLimits(const QDateTime &startDate, const QDateTime &endDate,
                     double ymin, double ymax) const;
  void setAxisLimits(double xmin, double xmax, double ymin, double ymax) const;

  [[nodiscard]] QGraphicsRectItem *infoRectItem() const;
  void setInfoRectItem(QGraphicsRectItem *infoRectItem);

  [[nodiscard]] QGraphicsTextItem *infoItem() const;
  void setInfoItem(QGraphicsTextItem *infoItem);

  void setTitleFontsize(int titleFontsize);

  void setLegendFontsize(int legendFontsize);

  void setAxisFontsize(int axisFontsize);

  void setDateFormat(const QString &format);
  void setDateFormat(const QDateTime &start, const QDateTime &end) const;

  static QStringList dateFormats();

public slots:
  void handleLegendMarkerClicked();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  void resetAxisLimits();
  void setBackground(bool b);

  QPointF dateDisplayPosition();

  static bool pointXLessThan(const QPointF &p1, const QPointF &p2);

  qreal m_xAxisMin, m_xAxisMax;
  qreal m_yAxisMin, m_yAxisMax;
  qreal m_currentXAxisMin, m_currentXAxisMax;
  qreal m_currentYAxisMin, m_currentYAxisMax;
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
  [[nodiscard]] bool isOnPlot(qreal x, qreal y) const;
  void addTraceLines(QMouseEvent *event);
  void addXTraceLine(QMouseEvent *event);
  void addXYTraceLine(QMouseEvent *event);
  void removeTraceLines();

  void addLineValuesToLegend(qreal x);

  void addChartPositionToLegend(qreal x, qreal y);

  void makeDynamicLegendLabels(qreal x, qreal y);

  void displayInstructionsOnStatusBar();

  void resetPlotLegend();
};

#endif // CHARTVIEW_H
