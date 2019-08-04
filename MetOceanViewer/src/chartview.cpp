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
#include "chartview.h"
#include <QDateTime>
#include <QLegendMarker>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtGui/QMouseEvent>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QGraphicsScene>
#include <QtWidgets/QGraphicsTextItem>
#include "timezone.h"

bool ChartView::pointXLessThan(const QPointF &p1, const QPointF &p2) {
  return p1.x() < p2.x();
}

ChartView::ChartView(QWidget *parent) : QChartView(new QChart(), parent) {
  this->m_coord = new QGraphicsSimpleTextItem(this->chart());
  this->m_yAxis = nullptr;
  this->m_dateAxis = nullptr;
  this->m_xAxis = nullptr;
  this->m_statusBar = nullptr;
  this->m_infoItem = nullptr;
  this->m_infoRectItem = nullptr;
  this->m_xTraceLinePtr = nullptr;
  this->m_yTraceLinePtr = nullptr;
  this->m_yTraceLine = QLineF();
  this->m_xTraceLine = QLineF();

  this->setDragMode(QChartView::NoDrag);
  this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  this->setMouseTracking(true);
  this->setDisplayValues(false);
  this->setRubberBand(QChartView::RectangleRubberBand);
  this->setRenderHint(QPainter::Antialiasing);

  this->chart()->setAnimationOptions(QChart::SeriesAnimations);
  this->chart()->legend()->setAlignment(Qt::AlignBottom);
  this->chart()->setTitleFont(QFont("Helvetica", 14, QFont::Bold));

  this->m_coord->setPos(this->size().width() / 2 - 100,
                        this->size().height() - 20);

  this->m_displayValues = false;
  this->m_style = 0;
  this->x_axis_max = 0.0;
  this->x_axis_min = 0.0;
  this->y_axis_max = 0.0;
  this->y_axis_min = 0.0;
  this->current_x_axis_max = 0.0;
  this->current_x_axis_min = 0.0;
  this->current_y_axis_max = 0.0;
  this->current_y_axis_min = 0.0;
}

ChartView::~ChartView() {}

void ChartView::initializeAxis(int style) {
  this->setStyle(style);
  if (style == 1) {
    if (this->m_dateAxis == nullptr) {
      this->m_dateAxis = new QDateTimeAxis(this->chart());
      this->chart()->addAxis(this->m_dateAxis, Qt::AlignBottom);
      this->dateAxis()->setTickCount(5);
      this->dateAxis()->setGridLineColor(QColor(200, 200, 200));
      this->dateAxis()->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
    }
  } else if (style == 2) {
    if (this->m_xAxis == nullptr) {
      this->m_xAxis = new QValueAxis(this->chart());
      this->chart()->addAxis(this->m_xAxis, Qt::AlignBottom);
      this->xAxis()->setTickCount(5);
      this->xAxis()->setGridLineColor(QColor(200, 200, 200));
      this->xAxis()->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
    }
  }
  if (this->m_yAxis == nullptr) {
    this->m_yAxis = new QValueAxis(this->chart());
    this->chart()->addAxis(this->m_yAxis, Qt::AlignLeft);
    this->yAxis()->setTickCount(10);
    this->yAxis()->setGridLineColor(QColor(200, 200, 200));
    this->yAxis()->setShadesPen(Qt::NoPen);
    this->yAxis()->setShadesBrush(QBrush(QColor(240, 240, 240)));
    this->yAxis()->setShadesVisible(true);
    this->yAxis()->setTitleFont(QFont("Helvetica", 10, QFont::Bold));
  }
  return;
}

void ChartView::setDateFormat(QDateTime start, QDateTime end) {
  if (start.daysTo(end) > 90)
    this->dateAxis()->setFormat("MM/yyyy");
  else if (start.daysTo(end) > 4)
    this->dateAxis()->setFormat("MM/dd/yyyy");
  else
    this->dateAxis()->setFormat("MM/dd/yyyy hh:mm");
  return;
}

void ChartView::setAxisLimits(double xmin, double xmax, double ymin,
                              double ymax) {
  this->xAxis()->setMin(xmin);
  this->xAxis()->setMax(xmax);
  this->yAxis()->setMin(ymin);
  this->yAxis()->setMax(ymax);
  return;
}

void ChartView::setAxisLimits(QDateTime startDate, QDateTime endDate,
                              double ymin, double ymax) {
  int offset = Timezone::localMachineOffsetFromUtc();
  if (startDate.timeSpec() == Qt::LocalTime)
    this->dateAxis()->setMin(startDate);
  else
    this->dateAxis()->setMin(startDate.addSecs(-offset));

  if (endDate.timeSpec() == Qt::LocalTime)
    this->dateAxis()->setMax(endDate);
  else
    this->dateAxis()->setMax(endDate.addSecs(-offset));

  this->yAxis()->setMin(ymin);
  this->yAxis()->setMax(ymax);
  return;
}

void ChartView::clear() {
  if (this->chart()->series().length() > 0) this->chart()->removeAllSeries();
  this->m_legendNames.clear();
  this->m_series.clear();
  this->removeTraceLines();
  return;
}

void ChartView::setDisplayValues(bool value) {
  this->m_displayValues = value;
  return;
}

void ChartView::initializeLegendMarkers() {
  for (int i = 0; i < this->chart()->legend()->markers().length(); i++)
    this->chart()->legend()->markers().at(i)->setFont(
        QFont("Helvetica", 10, QFont::Bold));

  foreach (QLegendMarker *marker, this->chart()->legend()->markers()) {
    // Disconnect possible existing connection to avoid multiple connections
    QObject::disconnect(marker, SIGNAL(clicked()), this,
                        SLOT(handleLegendMarkerClicked()));
    QObject::connect(marker, SIGNAL(clicked()), this,
                     SLOT(handleLegendMarkerClicked()));
  }
  return;
}

void ChartView::addSeries(QLineSeries *series, QString name) {
  this->m_series.push_back(series);
  this->m_legendNames.push_back(name);

  this->chart()->addSeries(series);
  if (this->xAxis() != nullptr)
    series->attachAxis(this->xAxis());
  else
    series->attachAxis(this->dateAxis());

  series->attachAxis(this->yAxis());
  return;
}

void ChartView::rebuild() {
  this->initializeAxisLimits();
  return;
}

void ChartView::resizeEvent(QResizeEvent *event) {
  if (scene()) {
    scene()->setSceneRect(QRect(QPoint(0, 0), event->size()));
    if (this->chart()) {
      this->resetAxisLimits();
      this->chart()->resize(event->size());
      this->m_coord->setPos(this->chart()->size().width() / 2 - 100,
                            this->chart()->size().height() - 20);
      if (this->m_displayValues) {
        this->removeTraceLines();
      }
    }
  }
  QChartView::resizeEvent(event);
  return;
}

void ChartView::removeTraceLines() {
  if (this->m_xTraceLinePtr != nullptr) {
    this->scene()->removeItem(this->m_xTraceLinePtr);
    this->m_xTraceLinePtr = nullptr;
  }
  if (this->m_yTraceLinePtr != nullptr) {
    this->scene()->removeItem(this->m_yTraceLinePtr);
    this->m_yTraceLinePtr = nullptr;
  }
  return;
}

void ChartView::addXTraceLine(QMouseEvent *event) {
  qreal ymin = this->chart()
                   ->mapToPosition(QPointF(this->current_x_axis_min,
                                           this->current_y_axis_min))
                   .y();
  qreal ymax = this->chart()
                   ->mapToPosition(QPointF(this->current_x_axis_max,
                                           this->current_y_axis_max))
                   .y();
  this->removeTraceLines();
  this->m_xTraceLine.setLine(event->pos().x(), ymin, event->pos().x(), ymax);
  this->m_xTraceLinePtr = this->scene()->addLine(this->m_xTraceLine);
  return;
}

void ChartView::addXYTraceLine(QMouseEvent *event) {
  QPointF min = this->chart()->mapToPosition(
      QPointF(this->current_x_axis_min, this->current_y_axis_min));
  QPointF max = this->chart()->mapToPosition(
      QPointF(this->current_x_axis_max, this->current_y_axis_max));
  this->removeTraceLines();
  this->m_xTraceLine.setLine(event->pos().x(), min.y(), event->pos().x(),
                             max.y());
  this->m_yTraceLine.setLine(min.x(), event->pos().y(), max.x(),
                             event->pos().y());
  this->m_xTraceLinePtr = this->scene()->addLine(this->m_xTraceLine);
  this->m_yTraceLinePtr = this->scene()->addLine(this->m_yTraceLine);
  return;
}

void ChartView::addTraceLines(QMouseEvent *event) {
  if (this->m_style == 1) {
    this->addXTraceLine(event);
  } else if (this->m_style == 2) {
    this->addXYTraceLine(event);
  }
  return;
}

bool ChartView::getNearestPointToCursor(qreal cursorXPosition, int seriesIndex,
                                        qreal &x, qreal &y) {
  QVector<QPointF> pv = this->m_series[seriesIndex]->pointsVector();
  qreal x_ll = pv.at(0).x();
  qreal x_ul = pv.last().x();

  if (cursorXPosition >= x_ll && cursorXPosition <= x_ul) {
    QPointF c = QPointF(cursorXPosition, 0);
    size_t i_min =
        std::lower_bound(pv.begin(), pv.end(), c, ChartView::pointXLessThan) -
        pv.begin();
    x = pv.at(i_min).x();
    y = pv.at(i_min).y();
    return true;
  } else {
    return false;
  }
}

void ChartView::addLineValuesToLegend(qreal x) {
  for (int i = 0; i < this->m_series.length(); i++) {
    qreal xv, yv;
    bool found = this->getNearestPointToCursor(x, i, xv, yv);
    if (found)
      this->chart()->series().at(i)->setName(this->m_legendNames.at(i) + ": " +
                                             QString::number(yv));
    else
      this->chart()->series().at(i)->setName(this->m_legendNames.at(i));
  }
  QDateTime date = QDateTime::fromMSecsSinceEpoch(x,Qt::UTC);
  QString dateString = QString("Date: ") + date.toString("MM/dd/yyyy hh:mm AP");
  this->m_coord->setText(dateString);
  return;
}

void ChartView::addChartPositionToLegend(qreal x, qreal y) {
  this->m_coord->setText(
      tr("Measured: %1     Modeled: %2     Diff: %3").arg(x).arg(y).arg(y - x));
}

bool ChartView::isOnPlot(qreal x, qreal y) {
  if (x < this->current_x_axis_max && x > this->current_x_axis_min &&
      y < this->current_y_axis_max && y > this->current_y_axis_min)
    return true;
  else
    return false;
}

void ChartView::makeDynamicLegendLabels(qreal x, qreal y) {
  if (this->m_style == 1) {
    this->addLineValuesToLegend(x);
  } else if (this->m_style == 2) {
    this->addChartPositionToLegend(x, y);
  }
  return;
}

void ChartView::displayInstructionsOnStatusBar() {
  if (this->m_statusBar)
    this->m_statusBar->showMessage(
        tr("Left click and drag to zoom in, Right click to zoom out, "
           "Double click to reset zoom"));
}

void ChartView::resetPlotLegend() {
  this->m_coord->setText("");
  if (this->m_statusBar) this->m_statusBar->clearMessage();
  for (int i = 0; i < this->m_series.length(); i++)
    this->chart()->series().at(i)->setName(this->m_legendNames.at(i));
  this->removeTraceLines();
}

void ChartView::mouseMoveEvent(QMouseEvent *event) {
  if (this->m_coord) {
    if (this->m_displayValues) {
      qreal x = this->chart()->mapToValue(event->pos()).x();
      qreal y = this->chart()->mapToValue(event->pos()).y();

      if (this->isOnPlot(x, y)) {
        this->makeDynamicLegendLabels(x, y);
        this->addTraceLines(event);
        this->displayInstructionsOnStatusBar();
      } else {
        this->resetPlotLegend();
      }
    } else {
      this->resetPlotLegend();
    }
  }

  QChartView::mouseMoveEvent(event);
  return;
}

void ChartView::mouseReleaseEvent(QMouseEvent *event) {
  QChartView::mouseReleaseEvent(event);
  if (this->chart()) this->resetAxisLimits();
  return;
}

void ChartView::mouseDoubleClickEvent(QMouseEvent *event) {
  QChartView::mouseDoubleClickEvent(event);
  if (this->chart()) this->resetZoom();
  return;
}

void ChartView::wheelEvent(QWheelEvent *event) {
  if (this->chart() == nullptr) return;

  if (event->delta() > 0)
    this->chart()->zoomIn();
  else if (event->delta() < 0)
    this->chart()->zoomOut();

  QChartView::wheelEvent(event);

  this->resetAxisLimits();

  return;
}

void ChartView::resetZoom() {
  if (this->chart()) {
    this->chart()->zoomReset();
    this->resetAxisLimits();
  }
  return;
}

void ChartView::resetAxisLimits() {
  if (this->chart()) {
    QRectF box = this->chart()->plotArea();
    this->current_x_axis_min = this->chart()->mapToValue(box.bottomLeft()).x();
    this->current_x_axis_max = this->chart()->mapToValue(box.topRight()).x();
    this->current_y_axis_min = this->chart()->mapToValue(box.bottomLeft()).y();
    this->current_y_axis_max = this->chart()->mapToValue(box.topRight()).y();
  }
  return;
}

QGraphicsTextItem *ChartView::infoItem() const { return m_infoItem; }

void ChartView::setInfoItem(QGraphicsTextItem *infoItem) {
  if (this->m_infoItem != nullptr) {
    this->scene()->removeItem(this->m_infoItem);
    delete this->m_infoItem;
  }
  this->m_infoItem = infoItem;
}

QGraphicsRectItem *ChartView::infoRectItem() const { return m_infoRectItem; }

void ChartView::setInfoRectItem(QGraphicsRectItem *infoRectItem) {
  if (this->m_infoRectItem != nullptr) {
    this->scene()->removeItem(this->m_infoRectItem);
    delete this->m_infoRectItem;
  }
  this->m_infoRectItem = infoRectItem;
}

QValueAxis *ChartView::yAxis() const { return this->m_yAxis; }

QValueAxis *ChartView::xAxis() const { return this->m_xAxis; }

QDateTimeAxis *ChartView::dateAxis() const { return this->m_dateAxis; }

QString ChartView::infoString() const { return this->m_infoString; }

void ChartView::setInfoString(QString regressionString,
                              QString correlationString,
                              QString standardDeviationString) {
  this->setInfoString(
      "<table><tr><td align=\"right\"><b> " + tr("Regression Line") +
      ": </b></td><td>" + regressionString + "</td></tr>" +
      "<tr><td align=\"right\"><b> " + tr("Correlation") +
      " (R&sup2;): </b></td><td>" + correlationString + "</td></tr>" +
      "<tr><td align=\"right\"><b> " + tr("Standard Deviation:") +
      " </b></td><td>" + standardDeviationString + "</td></tr></table>");
  return;
}

void ChartView::setInfoString(QString infoString) {
  this->m_infoString = infoString;
}

int ChartView::style() const { return this->m_style; }

void ChartView::setStyle(int style) { this->m_style = style; }

QGraphicsSimpleTextItem *ChartView::coord() const { return this->m_coord; }

void ChartView::initializeAxisLimits() {
  this->yAxis()->applyNiceNumbers();
  if (this->style() == 2) this->xAxis()->applyNiceNumbers();

  QRectF box = this->chart()->plotArea();
  this->x_axis_min = this->chart()->mapToValue(box.bottomLeft()).x();
  this->y_axis_min = this->chart()->mapToValue(box.bottomLeft()).y();
  this->x_axis_max = this->chart()->mapToValue(box.topRight()).x();
  this->y_axis_max = this->chart()->mapToValue(box.topRight()).y();
  this->current_x_axis_min = this->x_axis_min;
  this->current_x_axis_max = this->x_axis_max;
  this->current_y_axis_min = this->y_axis_min;
  this->current_y_axis_max = this->y_axis_max;
  this->current_x_axis_max = this->x_axis_max;
  this->current_y_axis_max = this->y_axis_max;
  this->current_x_axis_min = this->x_axis_min;
  this->current_y_axis_min = this->y_axis_min;
  return;
}

void ChartView::setStatusBar(QStatusBar *inStatusBar) {
  this->m_statusBar = inStatusBar;
  return;
}

void ChartView::handleLegendMarkerClicked() {
  QLegendMarker *marker = qobject_cast<QLegendMarker *>(sender());

  Q_ASSERT(marker);

  switch (marker->type()) {
    case QLegendMarker::LegendMarkerTypeXY: {
      // Toggle visibility of series
      marker->series()->setVisible(!marker->series()->isVisible());

      // Turn legend marker back to visible, since hiding series also hides the
      // marker and we don't want it to happen now.
      marker->setVisible(true);

      // Dim the marker, if series is not visible
      qreal alpha = 1.0;

      if (!marker->series()->isVisible()) alpha = 0.5;

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
