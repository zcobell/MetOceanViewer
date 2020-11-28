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
#include <fstream>
#include <string>
#include <unordered_map>

#include "boost/format.hpp"
#include "generic.h"
#include "netcdf.h"
#include "stringutil.h"

CrmsData::CrmsData(MovStation &station, QDateTime startDate, QDateTime endDate,
                   const std::vector<std::string> &header,
                   const std::unordered_map<std::string, size_t> &mapping,
                   const std::string &filename)
    : WaterData(station, startDate, endDate),
      m_filename(filename),
      m_header(header),
      m_mapping(mapping) {}

int CrmsData::retrieveData(Hmdf::HmdfData *data, Datum::VDatum datum) {
  Q_UNUSED(datum)
  int ncid;
  int ierr = nc_open(this->m_filename.c_str(), NC_NOWRITE, &ncid);

  qint64 minTime = this->startDate().toSecsSinceEpoch();
  qint64 maxTime = this->endDate().toSecsSinceEpoch();

  size_t index;
  auto sn = this->station().name().toStdString();
  auto it = this->m_mapping.find(sn);
  if (it != this->m_mapping.end()) {
    index = this->m_mapping[sn];
  } else {
    return 1;
  }

  int varid_data, varid_time, dimid_n, dimid_param;
  size_t n, np;

  std::string stationLengthString =
      boost::str(boost::format("stationLength_%6.6d") % (index + 1));

  std::string stationDataString =
      boost::str(boost::format("data_station_%6.6d") % (index + 1));

  std::string stationTimeString =
      boost::str(boost::format("time_station_%6.6d") % (index + 1));

  ierr += nc_inq_dimid(ncid, stationLengthString.c_str(), &dimid_n);
  ierr += nc_inq_dimid(ncid, "numParam", &dimid_param);
  ierr += nc_inq_dimlen(ncid, dimid_n, &n);
  ierr += nc_inq_dimlen(ncid, dimid_param, &np);
  ierr += nc_inq_varid(ncid, stationDataString.c_str(), &varid_data);
  ierr += nc_inq_varid(ncid, stationTimeString.c_str(), &varid_time);

  std::vector<long long> t(n);
  ierr += nc_get_var_longlong(ncid, varid_time, t.data());

  for (size_t i = 0; i < np; ++i) {
    std::vector<float> v(n);
    size_t start[2] = {i, 0};
    size_t count[2] = {1, n};
    ierr += nc_get_vara_float(ncid, varid_data, start, count, v.data());

    std::vector<double> tsdata;
    std::vector<long long> time;
    tsdata.reserve(n);
    time.reserve(n);

    for (size_t j = 0; j < n; ++j) {
      if (v[j] > -9999.0f && t[j] >= minTime && t[j] <= maxTime) {
        time.push_back(t[j] * 1000);
        tsdata.push_back(static_cast<double>(v[j]));
      }
    }

    if (tsdata.size() < 5) continue;

    Hmdf::Station s(0, this->station().coordinate().longitude(),
                    this->station().coordinate().latitude());
    s.setName(this->m_header[i]);
    s.setId(std::to_string(i));

    s.allocate(tsdata.size());
    for (size_t i = 0; i < tsdata.size(); ++i) {
      Hmdf::Date d;
      d.fromMSeconds(time[i]);
      s << Hmdf::Timepoint(d, tsdata[i]);
    }

    data->addStation(s);
  }

  return 0;
}

bool CrmsData::generateStationMapping(
    const std::string &filename,
    std::unordered_map<std::string, size_t> &mapping) {
  int ncid;
  int dimid_nstation, dimid_stringlen;
  size_t n, stringlen;

  int ierr = nc_open(filename.c_str(), NC_NOWRITE, &ncid);
  ierr += nc_inq_dimid(ncid, "nstation", &dimid_nstation);
  ierr += nc_inq_dimid(ncid, "stringsize", &dimid_stringlen);
  ierr += nc_inq_dimlen(ncid, dimid_nstation, &n);
  ierr += nc_inq_dimlen(ncid, dimid_stringlen, &stringlen);
  for (size_t i = 0; i < n; ++i) {
    int varid_station;

    std::string stationDataString =
        boost::str(boost::format("data_station_%6.6d") % (i + 1));

    ierr += nc_inq_varid(ncid, stationDataString.c_str(), &varid_station);
    std::string nm(" ", stringlen);
    ierr += nc_get_att_text(ncid, varid_station, "station_name", &nm[0]);
    std::string name = StringUtil::sanitizeString(nm);
    mapping[name] = i;
  }
  return ierr == 0;
}

bool CrmsData::readHeader(const std::string &filename,
                          std::vector<std::string> &header) {
  int ncid;
  int dimid_numParam, dimid_stringlen;
  int varid_sensors;
  size_t np, stringlen;
  int ierr = nc_open(filename.c_str(), NC_NOWRITE, &ncid);
  nc_inq_dimid(ncid, "numParam", &dimid_numParam);
  nc_inq_dimlen(ncid, dimid_numParam, &np);
  nc_inq_dimid(ncid, "stringsize", &dimid_stringlen);
  nc_inq_dimlen(ncid, dimid_stringlen, &stringlen);
  nc_inq_varid(ncid, "sensors", &varid_sensors);

  for (size_t i = 0; i < np; ++i) {
    std::string n(' ', stringlen);
    size_t start[2] = {i, 0};
    size_t count[2] = {1, stringlen};
    ierr += nc_get_vara_text(ncid, varid_sensors, start, count, &n[0]);
    QString h = QString::fromStdString(n);
    h = h.remove("\xEF\xBF\xBD");
    header.push_back(h.toStdString());
  }
  return ierr == 0;
}

bool CrmsData::readStationList(const std::string &filename,
                               std::vector<double> &latitude,
                               std::vector<double> &longitude,
                               std::vector<std::string> &stationNames,
                               std::vector<QDateTime> &startDate,
                               std::vector<QDateTime> &endDate) {
  QFile crmsCsv(":/stations/data/crms_stations.csv");
  if (!crmsCsv.open(QIODevice::ReadOnly)) {
    return false;
  }

  std::unordered_map<std::string, QGeoCoordinate> nameMap;

  while (!crmsCsv.atEnd()) {
    std::string s = crmsCsv.readLine().simplified().toStdString();
    std::vector<std::string> sl = StringUtil::stringSplitToVector(s, ",");
    double lon = stod(sl[0]);
    double lat = stod(sl[1]);
    QGeoCoordinate c(lat, lon);
    nameMap[sl[2]] = c;
  }
  crmsCsv.close();

  int ncid, dimid_nstation, dimid_stringsize;
  size_t nstations, stringsize;
  int ierr = nc_open(filename.c_str(), NC_NOWRITE, &ncid);
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
    std::string nm(' ', stringsize);
    std::string stationDataString =
        boost::str(boost::format("data_station_%6.6d") % (i + 1));

    std::string stationTimeString =
        boost::str(boost::format("time_station_%6.6lu") % (i + 1));

    ierr += nc_inq_varid(ncid, stationDataString.c_str(), &varid_data);
    ierr += nc_inq_varid(ncid, stationTimeString.c_str(), &varid_time);

    ierr += nc_get_att_text(ncid, varid_data, "station_name", &nm[0]);
    std::string name(nm);

    QGeoCoordinate p;
    auto it = nameMap.find(name);
    if (it != nameMap.end()) {
      p = nameMap[name];
    } else {
      continue;
    }

    std::string tms(' ', stringsize);
    std::string tme(' ', stringsize);

    ierr += nc_get_att_text(ncid, varid_time, "minimum", &tms[0]);
    ierr += nc_get_att_text(ncid, varid_time, "maximum", &tme[0]);

    QString dateBeginString = QString::fromStdString(tms);
    QString dateEndString = QString::fromStdString(tme);

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

bool CrmsData::inquireCrmsStatus(const std::string &filename) {
  std::ifstream f(filename.c_str());
  return f.good();
}
