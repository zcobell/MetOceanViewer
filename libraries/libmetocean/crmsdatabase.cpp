/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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
#include "crmsdatabase.h"
#include <QDateTime>
#include <QThread>
#include <iostream>
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/trim.hpp"
#include "boost/lexical_cast.hpp"
#include "hmdfasciiparser.h"
#include "netcdf.h"
#include "stringutil.h"
#include "timezone.h"

std::vector<std::string> splitString(const std::string &s) {
  std::vector<std::string> elems;
  boost::algorithm::split(elems, s, boost::is_any_of(","),
                          boost::token_compress_off);
  return elems;
}

CrmsDatabase::CrmsDatabase(const std::string &datafile,
                           const std::string &outputFile, QObject *parent)
    : m_databaseFile(datafile),
      m_outputFile(outputFile),
      m_hasError(false),
      m_showProgressBar(false),
      m_progressbar(nullptr),
      m_previousPercentComplete(0),
      QObject(parent) {}

double CrmsDatabase::getPercentComplete() {
  size_t fileposition = static_cast<size_t>(this->m_file.tellg());
  double percent =
      static_cast<double>(static_cast<long double>(fileposition) /
                          static_cast<long double>(this->m_fileLength)) *
      100.0;
  emit this->percentComplete(static_cast<int>(std::floor(percent)));
  if (this->m_showProgressBar) {
    unsigned long dt = static_cast<unsigned long>(std::floor(percent)) -
                       this->m_previousPercentComplete;
    if (dt > 0) {
      *(this->m_progressbar) += dt;
      this->m_previousPercentComplete += dt;
    }
  }
  return percent;
}

void CrmsDatabase::parse() {
  if (!this->fileExists(this->m_databaseFile)) {
    emit error("File does not exist");
    emit complete();
    return;
  }

  std::string name;
  std::vector<CrmsDataContainer> data;

  this->openCrmsFile();
  this->readHeader();
  this->initializeOutputFile();

  if (this->m_showProgressBar) {
    this->m_progressbar.reset(new boost::progress_display(100));
  }

  size_t nStation = 0;
  bool finished = false;

  while (!finished) {
    if (QThread::currentThread()->isInterruptionRequested()) {
      this->exitCleanly();
      break;
    }
    this->getPercentComplete();
    bool valid = this->getNextStation(data, finished);
    if (valid) {
      this->putNextStation(nStation, data);
    }
    nStation++;
    data.clear();
  }

  this->closeOutputFile(nStation);
  this->closeCrmsFile();

  if (this->m_hasError) {
    emit error("Error during CRMS processing");
    emit this->percentComplete(0);
  } else {
    emit success();
    emit this->percentComplete(100);
  }

  emit complete();
  return;
}

void CrmsDatabase::putNextStation(size_t stationNumber,
                                  std::vector<CrmsDataContainer> &data) {
  int dimid_param;
  int ierr = nc_inq_dimid(this->m_ncid, "numParam", &dimid_param);

  ierr = nc_redef(this->m_ncid);
  QString station_dim_string, station_time_var_string, station_data_var_string;
  station_dim_string.sprintf("stationLength_%6.6llu", stationNumber + 1);
  station_time_var_string.sprintf("time_station_%6.6llu", stationNumber + 1);
  station_data_var_string.sprintf("data_station_%6.6llu", stationNumber + 1);

  int dimid_len, varid_time, varid_data;
  ierr = nc_def_dim(this->m_ncid, station_dim_string.toStdString().c_str(),
                    data.size(), &dimid_len);
  int dims[2];
  dims[0] = dimid_param;
  dims[1] = dimid_len;

  ierr = nc_def_var(this->m_ncid, station_time_var_string.toStdString().c_str(),
                    NC_INT64, 1, &dimid_len, &varid_time);
  ierr = nc_def_var(this->m_ncid, station_data_var_string.toStdString().c_str(),
                    NC_FLOAT, 2, dims, &varid_data);

  ierr = nc_def_var_deflate(this->m_ncid, varid_time, 1, 1, 2);
  ierr = nc_def_var_deflate(this->m_ncid, varid_data, 1, 1, 2);

  ierr = nc_put_att_text(this->m_ncid, varid_data, "station_name",
                         data[0].id.length(), data[0].id.c_str());

  QDateTime refDate = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
  QString refstring =
      "seconds since " + refDate.toString("yyyy/MM/dd hh:mm:ss UTC");
  QString minString = data[0].datetime.toString("yyyy/MM/dd hh:mm:ss");
  QString maxString =
      data[data.size() - 1].datetime.toString("yyyy/MM/dd hh:mm:ss");

  ierr = nc_put_att_text(this->m_ncid, varid_time, "station_name",
                         data[0].id.length(), data[0].id.c_str());
  ierr = nc_put_att_text(this->m_ncid, varid_time, "reference",
                         refstring.length(), refstring.toStdString().c_str());
  ierr = nc_put_att_text(this->m_ncid, varid_time, "minimum",
                         minString.length(), minString.toStdString().c_str());
  ierr = nc_put_att_text(this->m_ncid, varid_time, "maximum",
                         maxString.length(), maxString.toStdString().c_str());

  this->m_stationLocations.push_back(data[0].location);
  float fill = this->fillValue();
  ierr = nc_def_var_fill(this->m_ncid, varid_data, 0, &fill);
  ierr = nc_enddef(this->m_ncid);

  size_t nData = data.size() * this->m_categoryMap.size();

  long long *t = new long long[data.size()];
  float *v = new float[nData];
  size_t idx = 0;

  for (size_t i = 0; i < data.size(); ++i) {
    t[i] = static_cast<long long>(data[i].datetime.toSecsSinceEpoch());
  }

  for (size_t i = 0; i < this->m_categoryMap.size(); ++i) {
    for (size_t j = 0; j < data.size(); ++j) {
      v[idx] = data[j].values[i];
      idx++;
    }
  }

  ierr = nc_put_var_longlong(this->m_ncid, varid_time, t);
  ierr = nc_put_var_float(this->m_ncid, varid_data, v);

  delete[] t;
  delete[] v;
  return;
}

void CrmsDatabase::openCrmsFile() {
  this->m_file.open(this->m_databaseFile, std::ios::binary);
  this->m_file.seekg(0, std::ios::end);
  this->m_fileLength = static_cast<size_t>(this->m_file.tellg());
  this->m_file.seekg(0, std::ios::beg);
}

void CrmsDatabase::closeOutputFile(size_t numStations) {
  int ierr = nc_redef(this->m_ncid);
  int dimid_nstation;
  ierr = nc_def_dim(this->m_ncid, "nstation", numStations, &dimid_nstation);
  ierr = nc_close(this->m_ncid);
  return;
}

void CrmsDatabase::closeCrmsFile() {
  if (this->m_file.is_open()) this->m_file.close();
  return;
}

void CrmsDatabase::readHeader() {
  std::string line;
  size_t idx = 0;
  std::getline(this->m_file, line);
  line = StringUtil::sanitizeString(line);
  std::vector<std::string> list = StringUtil::stringSplitToVector(line, ",");
  for (size_t i = 0; i < list.size(); ++i) {
    std::string s = list[i];
    if (s != "Station ID" && s != "Date (mm/dd/yyyy)" &&
        s != "Time (hh:mm:ss)" && s != "Time Zone" &&
        s != "Sensor Environment" && s != "Geoid" && s != "Organization Name" &&
        s != "Comments" && s != "Latitude" && s != "Longitude") {
      this->m_dataCategories.push_back(s);
      this->m_categoryMap[idx] = i;
      idx++;
    } else if (s == "Geoid") {
      this->m_geoidIndex = i;
    }
  }
  return;
}

CrmsDatabase::CrmsDataContainer CrmsDatabase::splitToCrmsDataContainer(
    const std::string &line) {
  CrmsDataContainer d;

  std::vector<std::string> split = splitString(line);
  d.id = split[0];

  QString dateString = QString::fromStdString(split[1]);
  QString timeString = QString::fromStdString(split[2]);

  d.datetime = QDateTime(QDate::fromString(dateString, "M/d/yyyy"),
                         QTime::fromString(timeString, "hh:mm:ss"), Qt::UTC);
  int offset = Timezone::offsetFromUtc(QString::fromStdString(split[3]));
  d.datetime = d.datetime.addSecs(-offset);

  if (!d.datetime.isValid()) {
    d.valid = false;
    return d;
  }

  d.geoid = split[this->m_geoidIndex];
  d.values.reserve(this->m_categoryMap.size());

  for (size_t i = 0; i < this->m_categoryMap.size(); ++i) {
    size_t idx = this->m_categoryMap[i];
    if (split[idx] == "") {
      d.values.push_back(this->fillValue());
    } else {
      try {
        // float v = boost::lexical_cast<float>(split[idx]);
        float v = std::stof(split[idx]);
        d.values.push_back(v);
      } catch (...) {
        d.values.push_back(this->fillValue());
      }
    }
  }
  d.valid = true;
  return d;
}

bool CrmsDatabase::getNextStation(std::vector<CrmsDataContainer> &data,
                                  bool &finished) {
  std::string prevname;
  size_t n = 0;

  data.reserve(300000);

  for (;;) {
    if (QThread::currentThread()->isInterruptionRequested()) {
      this->exitCleanly();
      finished = true;
      return false;
    }

    std::string line;

    std::getline(this->m_file, line);
    finished = this->m_file.eof();
    streampos p = this->m_file.tellg();

    CrmsDataContainer d = this->splitToCrmsDataContainer(line);

    if (n == 0) {
      prevname = d.id;
    } else if (prevname != d.id) {
      this->m_file.seekg(p);
      break;
    }

    n++;
    if (d.valid) data.push_back(d);
    if (finished) break;
  }
  return data.size() > 0;
}

bool CrmsDatabase::fileExists(const std::string &filename) {
  std::ifstream ifile(filename.c_str());
  return static_cast<bool>(ifile);
}

void CrmsDatabase::initializeOutputFile() {
  int ierr = nc_create(this->m_outputFile.c_str(), NC_NETCDF4, &this->m_ncid);
  int dimid_categories, dimid_stringsize, varid_cat;
  ierr = nc_def_dim(this->m_ncid, "numParam", this->m_categoryMap.size(),
                    &dimid_categories);
  ierr = nc_def_dim(this->m_ncid, "stringsize", 200, &dimid_stringsize);
  int dims[2];
  dims[0] = dimid_categories;
  dims[1] = dimid_stringsize;
  ierr = nc_def_var(this->m_ncid, "sensors", NC_CHAR, 2, dims, &varid_cat);
  ierr = nc_enddef(this->m_ncid);

  for (size_t i = 0; i < this->m_dataCategories.size(); ++i) {
    std::string s = this->m_dataCategories[i];
    const char *name = s.c_str();
    const size_t start[2] = {i, 0};
    const size_t count[2] = {1, s.length()};
    ierr = nc_put_vara_text(this->m_ncid, varid_cat, start, count, name);
  }
  return;
}

void CrmsDatabase::exitCleanly() {
  this->m_file.close();
  nc_close(this->m_ncid);
  this->m_hasError = true;
  return;
}

bool CrmsDatabase::showProgressBar() const { return this->m_showProgressBar; }

void CrmsDatabase::setShowProgressBar(bool showProgressBar) {
  this->m_showProgressBar = showProgressBar;
}
