#ifndef CRMSDATA_H
#define CRMSDATA_H

#include <QMap>
#include "waterdata.h"

class CrmsData : public WaterData {
  Q_OBJECT
 public:
  CrmsData(Station &station, QDateTime startDate, QDateTime endDate,
           const QVector<QString> &header, const QMap<QString, size_t> &mapping,
           const QString &filename, QObject *parent = nullptr);

  static bool readHeader(const QString &filename, QVector<QString> &header);

  static bool generateStationMapping(const QString &filename,
                                     QMap<QString, size_t> &mapping);

  static bool readStationList(const QString &filename,
                              QVector<double> &latitude,
                              QVector<double> &longitude,
                              QVector<QString> &stationNames,
                              QVector<QDateTime> &startDate,
                              QVector<QDateTime> &endDate);

  static bool inquireCrmsStatus(QString filename);

 private:
  int retrieveData(Hmdf *data);

  QDateTime m_startTime;
  QDateTime m_endTime;
  QString m_filename;
  QVector<QString> m_header;
  QMap<QString, size_t> m_mapping;
};

#endif  // CRMSDATA_H
