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
#include <iostream>
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/trim.hpp"
#include "boost/format.hpp"
#include "boost/lexical_cast.hpp"
#include "cdate.h"
#include "netcdf.h"

std::vector<std::string> splitString(const std::string &s) {
  std::vector<std::string> elems;
  boost::algorithm::split(elems, s, boost::is_any_of(","),
                          boost::token_compress_off);
  return elems;
}

CrmsDatabase::CrmsDatabase(const std::string &datafile,
                           const std::string &outputFile)
    : m_databaseFile(datafile),
      m_outputFile(outputFile),
      m_showProgressBar(true),
      m_previousPercentComplete(0),
      m_progressbar(nullptr) {}

double CrmsDatabase::getPercentComplete() {
  size_t fileposition = static_cast<size_t>(this->m_file.tellg());
  double percent =
      static_cast<double>(static_cast<long double>(fileposition) /
                          static_cast<long double>(this->m_fileLength)) *
      100.0;
  if (this->m_showProgressBar) {
    unsigned long dt = static_cast<unsigned long>(std::floor(percent)) -
                       this->m_previousPercentComplete;
    if (dt > 100 - this->m_previousPercentComplete) {
      dt = 100 - this->m_previousPercentComplete;
    }
    if (dt > 0) {
      *(this->m_progressbar) += dt;
      this->m_previousPercentComplete += dt;
    }
  }
  return percent;
}

void CrmsDatabase::parse() {
  if (!this->fileExists(this->m_databaseFile)) {
    std::cerr << "File does not exist." << std::endl;
    return;
  }

  this->openCrmsFile();
  this->readHeader();
  this->initializeOutputFile();

  if (this->m_showProgressBar) {
    this->m_progressbar.reset(new boost::progress_display(100));
  }

  size_t nStation = 0;
  bool finished = false;

  while (!finished) {
    this->getPercentComplete();
    std::vector<CrmsDataContainer *> data;
    bool valid = this->getNextStation(data, finished);
    if (valid) {
      this->putNextStation(nStation, data);
    }
    this->deleteCrmsObjects(data);
    nStation++;
  }

  if (this->m_showProgressBar) {
    this->getPercentComplete();
  }

  this->closeOutputFile(nStation);
  this->closeCrmsFile();

  return;
}

void CrmsDatabase::deleteCrmsObjects(
    const std::vector<CrmsDataContainer *> &data) {
  for (auto &d : data) {
    delete d;
  }
  return;
}

void CrmsDatabase::putNextStation(size_t stationNumber,
                                  std::vector<CrmsDataContainer *> &data) {
  int dimid_param;
  int ierr = nc_inq_dimid(this->m_ncid, "numParam", &dimid_param);

  ierr = nc_redef(this->m_ncid);

  std::string station_dim_string =
      boost::str(boost::format("stationLength_%06i") % (stationNumber + 1));
  std::string station_time_var_string =
      boost::str(boost::format("time_station_%06i") % (stationNumber + 1));
  std::string station_data_var_string =
      boost::str(boost::format("data_station_%06i") % (stationNumber + 1));

  int dimid_len, varid_time, varid_data;
  ierr = nc_def_dim(this->m_ncid, station_dim_string.c_str(), data.size(),
                    &dimid_len);
  int dims[2];
  dims[0] = dimid_param;
  dims[1] = dimid_len;

  ierr = nc_def_var(this->m_ncid, station_time_var_string.c_str(), NC_INT64, 1,
                    &dimid_len, &varid_time);
  ierr = nc_def_var(this->m_ncid, station_data_var_string.c_str(), NC_FLOAT, 2,
                    dims, &varid_data);

  ierr = nc_def_var_deflate(this->m_ncid, varid_time, 1, 1, 2);
  ierr = nc_def_var_deflate(this->m_ncid, varid_data, 1, 1, 2);

  ierr = nc_put_att_text(this->m_ncid, varid_data, "station_name",
                         data[0]->id().length(), data[0]->id().c_str());

  CDate refDate, dateMin, dateMax;
  refDate.fromSeconds(0);
  dateMin.fromSeconds(data.front()->datetime());
  dateMax.fromSeconds(data.back()->datetime());

  std::string refstring = "seconds since " + refDate.toString() + " UTC";
  std::string minString = dateMin.toString();
  std::string maxString = dateMax.toString();

  ierr = nc_put_att_text(this->m_ncid, varid_time, "station_name",
                         data[0]->id().length(), data[0]->id().c_str());
  ierr = nc_put_att_text(this->m_ncid, varid_time, "reference",
                         refstring.length(), refstring.c_str());
  ierr = nc_put_att_text(this->m_ncid, varid_time, "minimum",
                         minString.length(), minString.c_str());
  ierr = nc_put_att_text(this->m_ncid, varid_time, "maximum",
                         maxString.length(), maxString.c_str());

  float fill = this->fillValue();
  ierr = nc_def_var_fill(this->m_ncid, varid_data, 0, &fill);
  ierr = nc_enddef(this->m_ncid);

  size_t nData = data.size() * this->m_categoryMap.size();

  long long *t = new long long[data.size()];
  float *v = new float[nData];
  size_t idx = 0;

  for (size_t i = 0; i < data.size(); ++i) {
    t[i] = data[i]->datetime();
  }

  for (size_t i = 0; i < this->m_categoryMap.size(); ++i) {
    for (size_t j = 0; j < data.size(); ++j) {
      v[idx] = data[j]->value(i);
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
  std::vector<std::string> list = splitString(line);
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

CrmsDataContainer *CrmsDatabase::splitToCrmsDataContainer(
    const std::string &line) {
  CrmsDataContainer *d = new CrmsDataContainer(this->m_categoryMap.size());

  std::vector<std::string> split = splitString(line);
  d->setId(split[0]);

  CDate date = CDate(split[1], split[2]);

  int offset = 0;
  if (split[3] == "CST") {
    offset = 21600;
  } else if (split[4] == "CDT") {
    offset = 18000;
  }
  date.add(offset);
  d->setDatetime(date.toSeconds());

  for (size_t i = 0; i < this->m_categoryMap.size(); ++i) {
    size_t idx = this->m_categoryMap[i];
    if (split[idx] == "") {
      d->setValue(i, this->fillValue());
    } else {
      try {
        // float v = boost::lexical_cast<float>(split[idx]);
        float v = std::stof(split[idx]);
        d->setValue(i, v);
      } catch (...) {
        d->setValue(i, this->fillValue());
      }
    }
  }
  d->setValid(true);
  return d;
}

bool CrmsDatabase::getNextStation(std::vector<CrmsDataContainer *> &data,
                                  bool &finished) {
  std::string prevname;
  size_t n = 0;

  for (;;) {
    std::string line;
    std::getline(this->m_file, line);
    finished = this->m_file.eof();
    std::streampos p = this->m_file.tellg();

    if (line.size() < 10) break;

    CrmsDataContainer *d = this->splitToCrmsDataContainer(line);

    if (n == 0) {
      prevname = d->id();
    } else if (prevname != d->id()) {
      this->m_file.seekg(p);
      delete d;
      break;
    }

    n++;
    if (d->valid()) data.push_back(d);
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

bool CrmsDatabase::showProgressBar() const { return this->m_showProgressBar; }

void CrmsDatabase::setShowProgressBar(bool showProgressBar) {
  this->m_showProgressBar = showProgressBar;
}
