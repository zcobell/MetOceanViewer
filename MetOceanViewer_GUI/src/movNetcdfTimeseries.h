#ifndef MOVNETCDFTIMESERIES_H
#define MOVNETCDFTIMESERIES_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include "movImeds.h"

class MovNetcdfTimeseries : public QObject {
  Q_OBJECT
 public:
  explicit MovNetcdfTimeseries(QObject *parent = nullptr);

  int read();

  int toImeds(MovImeds *imeds);

  QString filename() const;
  void setFilename(const QString &filename);

  int epsg() const;
  void setEpsg(int epsg);

  static int getEpsg(QString file);

 private:
  QString m_filename;
  QString m_units;
  QString m_verticalDatum;
  QString m_horizontalProjection;
  int m_epsg;
  size_t m_numStations;

  QVector<double> m_xcoor;
  QVector<double> m_ycoor;
  QVector<size_t> m_stationLength;
  QVector<QString> m_stationName;
  QVector<QVector<QDateTime> > m_time;
  QVector<QVector<double> > m_data;
};

#endif  // MOVNETCDFTIMESERIES_H
