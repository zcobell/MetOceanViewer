#ifndef NETCDFTIMESERIES_H
#define NETCDFTIMESERIES_H

#include <QDateTime>
#include <QObject>
#include <QVector>
#include "imeds.h"

class NetcdfTimeseries : public QObject {
  Q_OBJECT
 public:
  explicit NetcdfTimeseries(QObject *parent = nullptr);

  int read();

  int toImeds(Imeds *imeds);

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
  QVector<QVector<long long> > m_time;
  QVector<QVector<double> > m_data;
};

#endif  // NETCDFTIMESERIES_H
