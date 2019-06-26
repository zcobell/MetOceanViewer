#ifndef CRMSDATA_H
#define CRMSDATA_H

#include "waterdata.h"

class CrmsData : public WaterData {
  Q_OBJECT
 public:
  CrmsData(Station &station, QDateTime startDate, QDateTime endDate,
           QObject *parent = nullptr);

  void setDatabaseLocation();
  QString databaseLocation();

  static bool readStationList(const QString &filename,
                              QVector<double> &latitude,
                              QVector<double> &longitude,
                              QVector<QString> &stationNames);

  static bool inquireCrmsStatus(QString filename);

 private:
  int retrieveData(Hmdf *data);
};

#endif  // CRMSDATA_H
