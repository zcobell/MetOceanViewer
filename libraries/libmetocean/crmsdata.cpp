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
#include "crmsdata.h"
#include <QFileInfo>
#include <QGeoCoordinate>
#include <QMap>
#include <QString>
#include <QStringList>
#include "boost/algorithm/string.hpp"
#include "netcdf.h"

CrmsData::CrmsData(Station &station, QDateTime startDate, QDateTime endDate,
                   const QVector<QString> &header,
                   const QMap<QString, size_t> &mapping,
                   const QString &filename, QObject *parent)
    : m_mapping(mapping),
      m_header(header),
      m_filename(filename),
      WaterData(station, startDate, endDate, parent) {}

int CrmsData::retrieveData(Hmdf *data, Datum::VDatum datum) {
  Q_UNUSED(datum)
  int ncid;
  int ierr = nc_open(this->m_filename.toStdString().c_str(), NC_NOWRITE, &ncid);

  qint64 minTime = this->startDate().toSecsSinceEpoch();
  qint64 maxTime = this->endDate().toSecsSinceEpoch();

  size_t index;
  if (this->m_mapping.contains(this->station().name())) {
    index = this->m_mapping[this->station().name()];
  } else {
    nc_close(ncid);
    return 1;
  }

  int varid_data, varid_time, dimid_n, dimid_param;
  size_t n, np;
  QString stationDataString, stationTimeString, stationLengthString;
  stationLengthString.sprintf("stationLength_%6.6llu", index + 1);
  stationDataString.sprintf("data_station_%6.6llu", index + 1);
  stationTimeString.sprintf("time_station_%6.6llu", index + 1);
  ierr +=
      nc_inq_dimid(ncid, stationLengthString.toStdString().c_str(), &dimid_n);
  ierr += nc_inq_dimid(ncid, "numParam", &dimid_param);
  ierr += nc_inq_dimlen(ncid, dimid_n, &n);
  ierr += nc_inq_dimlen(ncid, dimid_param, &np);
  ierr +=
      nc_inq_varid(ncid, stationDataString.toStdString().c_str(), &varid_data);
  ierr +=
      nc_inq_varid(ncid, stationTimeString.toStdString().c_str(), &varid_time);

  std::vector<long long> t(n, 0);
  ierr += nc_get_var_longlong(ncid, varid_time, t.data());

  for (size_t i = 0; i < np; ++i) {
    std::vector<float> v(n, 0.0);
    size_t start[2] = {i, 0};
    size_t count[2] = {1, n};
    ierr += nc_get_vara_float(ncid, varid_data, start, count, v.data());
    HmdfStation *s = new HmdfStation(data);

    QVector<double> tsdata;
    QVector<long long> time;
    tsdata.reserve(n);
    time.reserve(n);

    for (size_t j = 0; j < n; ++j) {
      if (v[j] > -9999.0f && t[j] >= minTime && t[j] <= maxTime) {
        time.push_back(t[j] * 1000);
        tsdata.push_back(static_cast<double>(v[j]));
      }
    }

    if (tsdata.length() < 5) continue;

    s->setName(this->m_header[i]);
    s->setLongitude(this->station().coordinate().longitude());
    s->setLatitude(this->station().coordinate().latitude());
    s->setId(QString::number(i));
    s->setData(tsdata);
    s->setDate(time);
    s->setIsNull(false);

    data->addStation(s);
  }

  nc_close(ncid);
  return 0;
}

bool CrmsData::generateStationMapping(const QString &filename,
                                      QMap<QString, size_t> &mapping) {
  int ncid;
  int dimid_nstation, dimid_stringlen;
  size_t n, stringlen;

  int ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  ierr += nc_inq_dimid(ncid, "nstation", &dimid_nstation);
  ierr += nc_inq_dimid(ncid, "stringsize", &dimid_stringlen);
  ierr += nc_inq_dimlen(ncid, dimid_nstation, &n);
  ierr += nc_inq_dimlen(ncid, dimid_stringlen, &stringlen);
  for (size_t i = 0; i < n; ++i) {
    int varid_station;
    QString stationDataString;
    stationDataString.sprintf("data_station_%6.6llu", i + 1);
    ierr += nc_inq_varid(ncid, stationDataString.toStdString().c_str(),
                         &varid_station);
    std::string nm(stringlen, ' ');
    ierr += nc_get_att_text(ncid, varid_station, "station_name", &nm[0]);
    boost::trim_right(nm);
    nm.erase(std::find(nm.begin(), nm.end(), '\0'), nm.end());
    QString name = QByteArray::fromStdString(nm);
    mapping[name] = i;
  }
  nc_close(ncid);
  return ierr == 0;
}

bool CrmsData::readHeader(const QString &filename, QVector<QString> &header) {
  int ncid;
  int dimid_numParam, dimid_stringlen;
  int varid_sensors;
  size_t np, stringlen;
  int ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  nc_inq_dimid(ncid, "numParam", &dimid_numParam);
  nc_inq_dimlen(ncid, dimid_numParam, &np);
  nc_inq_dimid(ncid, "stringsize", &dimid_stringlen);
  nc_inq_dimlen(ncid, dimid_stringlen, &stringlen);
  nc_inq_varid(ncid, "sensors", &varid_sensors);

  for (size_t i = 0; i < np; ++i) {
    std::string n(stringlen, ' ');
    size_t start[2] = {i, 0};
    size_t count[2] = {1, stringlen};
    ierr += nc_get_vara_text(ncid, varid_sensors, start, count, &n[0]);
    boost::trim_right(n);
    n.erase(std::find(n.begin(), n.end(), '\0'), n.end());
    QString h = QByteArray::fromStdString(n);
    header.push_back(h);
  }
  nc_close(ncid);
  return ierr == 0;
}

bool CrmsData::readStationList(const QString &filename,
                               QVector<double> &latitude,
                               QVector<double> &longitude,
                               QVector<QString> &stationNames,
                               QVector<QDateTime> &startDate,
                               QVector<QDateTime> &endDate) {
  QFile crmsCsv(":/stations/data/crms_stations.csv");
  if (!crmsCsv.open(QIODevice::ReadOnly)) {
    return false;
  }

  QMap<QString, QGeoCoordinate> nameMap;

  while (!crmsCsv.atEnd()) {
    QString s = crmsCsv.readLine().simplified();
    QStringList sl = s.split(",");
    double lon = sl[0].toDouble();
    double lat = sl[1].toDouble();
    QGeoCoordinate c(lat, lon);
    nameMap[sl[2]] = c;
  }
  crmsCsv.close();

  int ncid, dimid_nstation, dimid_stringsize;
  size_t nstations, stringsize;
  int ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  ierr += nc_inq_dimid(ncid, "nstation", &dimid_nstation);
  ierr += nc_inq_dimlen(ncid, dimid_nstation, &nstations);
  ierr += nc_inq_dimid(ncid, "stringsize", &dimid_stringsize);
  ierr += nc_inq_dimlen(ncid, dimid_stringsize, &stringsize);

  longitude.reserve(nstations);
  latitude.reserve(nstations);
  startDate.reserve(nstations);
  endDate.reserve(nstations);
  stationNames.reserve(nstations);

  for (size_t i = 0; i < nstations; ++i) {
    int varid_data, varid_time;
    std::string nm(stringsize, ' ');

    QString stationDataString, stationTimeString;
    stationDataString.sprintf("data_station_%6.6llu", i + 1);
    stationTimeString.sprintf("time_station_%6.6llu", i + 1);
    ierr += nc_inq_varid(ncid, stationDataString.toStdString().c_str(),
                         &varid_data);
    ierr += nc_inq_varid(ncid, stationTimeString.toStdString().c_str(),
                         &varid_time);

    ierr += nc_get_att_text(ncid, varid_data, "station_name", &nm[0]);

    boost::trim_right(nm);
    nm.erase(std::find(nm.begin(), nm.end(), '\0'), nm.end());
    auto name = QByteArray::fromStdString(nm);

    QGeoCoordinate p;
    if (nameMap.contains(name)) {
      p = nameMap[name];
    } else {
      continue;
    }

    std::string tms(stringsize, ' ');
    std::string tme(stringsize, ' ');

    ierr += nc_get_att_text(ncid, varid_time, "minimum", &tms[0]);
    ierr += nc_get_att_text(ncid, varid_time, "maximum", &tme[0]);

    boost::trim_right(tms);
    tms.erase(std::find(tms.begin(), tms.end(), '\0'), tms.end());
    auto dateBeginString = QByteArray::fromStdString(tms);

    boost::trim_right(tme);
    tme.erase(std::find(tme.begin(), tme.end(), '\0'), tme.end());
    auto dateEndString = QString::fromStdString(tme);

    QDateTime dateBegin =
        QDateTime::fromString(dateBeginString, "yyyy/MM/dd hh:mm:ss");
    dateBegin.setTimeSpec(Qt::UTC);
    QDateTime dateEnd =
        QDateTime::fromString(dateEndString, "yyyy/MM/dd hh:mm:ss");
    dateEnd.setTimeSpec(Qt::UTC);

    latitude.push_back(p.latitude());
    longitude.push_back(p.longitude());
    startDate.push_back(dateBegin);
    endDate.push_back(dateEnd);
    stationNames.push_back(name);
  }
  ierr += nc_close(ncid);

  return ierr == 0;
}

bool CrmsData::inquireCrmsStatus(QString filename) {
  QFileInfo f(filename);
  return f.exists();
}
