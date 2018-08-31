#ifndef DRIVER_H
#define DRIVER_H

#include <QDateTime>
#include <QObject>
#include "hmdf.h"
#include "station.h"
#include "stationlocations.h"

class MetOceanData : public QObject {
  Q_OBJECT
 public:
  enum serviceTypes { NOAA, USGS, NDBC, XTIDE, UNKNOWNSERVICE };

  explicit MetOceanData(QObject *parent = nullptr);
  explicit MetOceanData(serviceTypes service, QStringList station, int product,
                        int datum, QDateTime startDate, QDateTime endDate,
                        QString outputFile, QObject *parent = nullptr);

  static QStringList selectStations(serviceTypes service, double x1, double y1,
                                    double x2, double y2);

  static QString selectNearestStation(serviceTypes service, double x, double y);

  int service() const;
  void setService(int service);

  QStringList station() const;
  void setStation(QStringList station);

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
  void warning(QString);

 public slots:
  virtual void run();
  void showError(QString);
  void showStatus(QString, int);
  void showWarning(QString);

 private:
  void getNoaaData();
  void getUsgsData();
  void getNdbcData();
  void getXtideData();

  bool findStation(QStringList name, StationLocations::MarkerType type,
                   QVector<Station> &s);
  static StationLocations::MarkerType serviceToMarkerType(serviceTypes type);

  QString noaaIndexToProduct();
  QString noaaIndexToDatum();
  QString noaaIndexToUnits();

  int printAvailableProducts(Hmdf *data);

  int m_service;
  QStringList m_station;
  int m_product;
  int m_datum;
  QDateTime m_startDate;
  QDateTime m_endDate;
  QString m_outputFile;
};

#endif  // DRIVER_H
