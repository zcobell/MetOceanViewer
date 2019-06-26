#ifndef CRMSDATA_H
#define CRMSDATA_H

#include "waterdata.h"

class CrmsData : public WaterData {
 public:
  CrmsData(Station &station, QDateTime startDate, QDateTime endDate,
           QObject *parent = nullptr);

  void setDatabaseLocation();
  QString databaseLocation();

 private:
  int retrieveData(Hmdf *data);
};

#endif  // CRMSDATA_H
