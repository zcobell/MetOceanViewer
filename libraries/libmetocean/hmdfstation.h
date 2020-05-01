/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#ifndef HMDFSTATION
#define HMDFSTATION

#include <QGeoCoordinate>
#include <QObject>
#include <QString>
#include <QVector>
#include "datum.h"
#include "metocean_global.h"
#include "station.h"

class HmdfStation : public QObject {
  Q_OBJECT

 public:
  explicit METOCEANSHARED_EXPORT HmdfStation(QObject *parent = nullptr);

  void METOCEANSHARED_EXPORT clear();

  static constexpr double METOCEANSHARED_EXPORT nullDataValue() {
    return -std::numeric_limits<double>::max();
  }

  static constexpr qint64 METOCEANSHARED_EXPORT nullDateValue() {
    return -std::numeric_limits<qint64>::max();
  }

  QGeoCoordinate METOCEANSHARED_EXPORT *coordinate();
  void METOCEANSHARED_EXPORT setCoordinate(const QGeoCoordinate coordinate);

  double METOCEANSHARED_EXPORT latitude() const;
  double METOCEANSHARED_EXPORT longitude() const;

  void METOCEANSHARED_EXPORT setLatitude(const double latitude);
  void METOCEANSHARED_EXPORT setLongitude(const double longitude);

  QString METOCEANSHARED_EXPORT name() const;
  void METOCEANSHARED_EXPORT setName(const QString &name);

  QString METOCEANSHARED_EXPORT id() const;
  void METOCEANSHARED_EXPORT setId(const QString &id);

  size_t METOCEANSHARED_EXPORT numSnaps() const;

  int METOCEANSHARED_EXPORT stationIndex() const;
  void METOCEANSHARED_EXPORT setStationIndex(int stationIndex);

  qint64 METOCEANSHARED_EXPORT date(int index) const;
  void METOCEANSHARED_EXPORT setDate(const qint64 &date, int index);
  void METOCEANSHARED_EXPORT setDate(const QVector<qint64> &date);

  void METOCEANSHARED_EXPORT setNext(const qint64 &date, const double &data);

  bool METOCEANSHARED_EXPORT isNull() const;
  void METOCEANSHARED_EXPORT setIsNull(bool isNull);

  double METOCEANSHARED_EXPORT data(int index) const;
  void METOCEANSHARED_EXPORT setData(const double &data, int index);
  void METOCEANSHARED_EXPORT setData(const QVector<double> &data);
  void METOCEANSHARED_EXPORT setData(const QVector<float> &data);

  QVector<qint64> METOCEANSHARED_EXPORT allDate() const;
  QVector<double> METOCEANSHARED_EXPORT allData() const;

  void METOCEANSHARED_EXPORT dataBounds(qint64 &minDate, qint64 &maxDate,
                                        double &minValue, double &maxValue);

  double METOCEANSHARED_EXPORT nullValue() const;
  void METOCEANSHARED_EXPORT setNullValue(double nullValue);

  int METOCEANSHARED_EXPORT applyDatumCorrection(Station s,
                                                 Datum::VDatum datum);

 private:
  QGeoCoordinate m_coordinate;

  QString m_name;
  QString m_id;

  int m_stationIndex;

  double m_nullValue;

  QVector<qint64> m_date;
  QVector<double> m_data;

  bool m_isNull;
};

#endif  // HMDFSTATION
