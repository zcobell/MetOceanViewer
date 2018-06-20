#ifndef TIDEPREDICTION_H
#define TIDEPREDICTION_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include "hmdf.h"
#include "station.h"

class TidePrediction : public QObject {
  Q_OBJECT
 public:
  explicit TidePrediction(QString root, QObject *parent = nullptr);

  ~TidePrediction();

  void deleteHarmonicsOnExit(bool b);

  int get(Station &s, QDateTime startDate, QDateTime endDate, int interval,
          Hmdf *data);

 private:
  void initHarmonicsDatabase();

  bool m_deleteHarmonicsOnExit = true;

  QString m_harmonicsDatabase;
};

#endif  // TIDEPREDICTION_H
