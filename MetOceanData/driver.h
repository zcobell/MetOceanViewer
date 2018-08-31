#ifndef DRIVER_H
#define DRIVER_H

#include <QDateTime>
#include <QObject>
#include "hmdf.h"
#include "station.h"
#include "stationlocations.h"

class Driver : public QObject {
  Q_OBJECT
 public:
  enum serviceTypes { NOAA, USGS, NDBC, XTIDE, UNKNOWNSERVICE };

  explicit Driver(QObject *parent = nullptr);
  explicit Driver(serviceTypes service, QString station, int product, int datum,
                  QDateTime startDate, QDateTime endDate, QString outputFile,
                  QObject *parent = nullptr);

  int service() const;
  void setService(int service);

  QString station() const;
  void setStation(QString station);

  QDateTime startDate() const;
  void setStartDate(const QDateTime &startDate);

  QDateTime endDate() const;
  void setEndDate(const QDateTime &endDate);

  QString outputFile() const;
  void setOutputFile(const QString &outputFile);

  void setLoggingActive();
  void setLoggingInactive();

  int getDatum() const;
  void setDatum(int datum);

 signals:
  void finished();
  void status(QString, int);
  void error(QString);

 public slots:
  virtual void run();
  void showError(QString);
  void showStatus(QString, int);

 private:
  void getNoaaData();
  void getUsgsData();
  void getNdbcData();
  void getXtideData();

  bool findStation(QString name, StationLocations::MarkerType type, Station &s);

  QString noaaIndexToProduct();
  QString noaaIndexToDatum();
  QString noaaIndexToUnits();

  int printAvailableProducts(Hmdf *data);

  int m_service;
  QString m_station;
  int m_product;
  int m_datum;
  QDateTime m_startDate;
  QDateTime m_endDate;
  QString m_outputFile;
};

#endif  // DRIVER_H
