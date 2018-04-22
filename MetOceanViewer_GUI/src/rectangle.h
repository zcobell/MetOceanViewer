#ifndef RECTANGLE_H
#define RECTANGLE_H

#include <QRectF>

class Rectangle : public QRectF {
 public:
  Rectangle();

  qreal area();

  int extend(QPointF point);

  int expand(qreal percent);

  bool containsPoint(const QPointF &p);
};

#endif  // RECTANGLE_H
