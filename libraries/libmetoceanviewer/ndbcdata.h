#ifndef NDBCDATA_H
#define NDBCDATA_H

#include <QMap>
#include "waterdata.h"

class NdbcData : public WaterData {
 public:
  NdbcData(Station &station, QDateTime startDate, QDateTime endDate,
           QObject *parent);

 private:
  int retrieveData(Hmdf *data);
  void buildDataNameMap();
  int download(QUrl url, QVector<QStringList> &dldata);
  int readNdbcResponse(QNetworkReply *reply, QVector<QStringList> &data);
  int formatNdbcResponse(QVector<QStringList> &serverResponse, Hmdf *data);

  QMap<QString, QString> m_dataNameMap;
};

#endif  // NDBCDATA_H
