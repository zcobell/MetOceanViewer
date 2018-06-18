#ifndef TIDEPREDICTION_H
#define TIDEPREDICTION_H

#include <QDateTime>
#include <QObject>
#include <QVector>

class TidePrediction : public QObject {
  Q_OBJECT
 public:
  explicit TidePrediction(QObject *parent = nullptr);

  static int get(QString stationName, QDateTime startDate, QDateTime endDate,
                 int interval, QVector<qint64> &date, QVector<double> &data);
};

#endif  // TIDEPREDICTION_H
