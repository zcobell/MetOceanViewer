#include "crmsdatabase.h"
#include <QDateTime>
#include <QDebug>
#include <iostream>
#include "boost/algorithm/string/split.hpp"
#include "boost/algorithm/string/trim.hpp"
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
    : m_databaseFile(datafile), m_outputFile(outputFile), QObject(parent) {}

double CrmsDatabase::fillValue() const { return -9999.0; }

int CrmsDatabase::parse() {
  if (!this->fileExists(this->m_databaseFile)) {
    std::cout << "Error: database file does not exist.";
    return 1;
  }

  int ncid;
  std::string name;
  std::vector<CrmsDataContainer> data;

  this->openCrmsFile();
  this->readHeader();
  this->initializeOutputFile(ncid);

  size_t nStation = 0;
  while (!this->m_file.eof()) {
    bool valid = this->getNextStation(data);
    if (valid) {
      this->putNextStation(ncid, nStation, data);
      qDebug() << QString::fromStdString(data.at(0).id);
    }
    nStation++;
    data.clear();
  }

  this->closeOutputFile(ncid, nStation);
  this->closeCrmsFile();

  return 0;
}

void CrmsDatabase::putNextStation(int ncid, size_t stationNumber,
                                  std::vector<CrmsDataContainer> &data) {
  int dimid_param;
  int ierr = nc_inq_dimid(ncid, "numParam", &dimid_param);

  ierr = nc_redef(ncid);
  QString station_dim_string, station_time_var_string, station_data_var_string;
  station_dim_string.sprintf("stationLength_%4.4llu", stationNumber + 1);
  station_time_var_string.sprintf("time_station_%4.4llu", stationNumber + 1);
  station_data_var_string.sprintf("data_station_%4.4llu", stationNumber + 1);

  int dimid_len, varid_time, varid_data;
  ierr = nc_def_dim(ncid, station_dim_string.toStdString().c_str(), data.size(),
                    &dimid_len);
  int dims[2];
  dims[0] = dimid_param;
  dims[1] = dimid_len;

  ierr = nc_def_var(ncid, station_time_var_string.toStdString().c_str(),
                    NC_INT64, 1, &dimid_len, &varid_time);
  ierr = nc_def_var(ncid, station_data_var_string.toStdString().c_str(),
                    NC_DOUBLE, 2, dims, &varid_data);
  ierr = nc_def_var_deflate(ncid, varid_time, 1, 1, 2);
  ierr = nc_def_var_deflate(ncid, varid_data, 1, 1, 2);
  ierr = nc_put_att_text(ncid, varid_data, "station_name", data[0].id.length(),
                         data[0].id.c_str());
  double fill = this->fillValue();
  ierr = nc_def_var_fill(ncid, varid_data, 0, &fill);
  ierr = nc_enddef(ncid);

  size_t nData = data.size() * this->m_dataCategories.size() - 7;

  long long *t = new long long[data.size()];
  double *v = new double[nData];
  size_t idx = 0;

  for (size_t i = 0; i < data.size(); ++i) {
    t[i] = static_cast<long long>(data[i].datetime.toSecsSinceEpoch());
  }

  for (size_t i = 5; i < this->m_dataCategories.size() - 2; ++i) {
    for (size_t j = 0; j < data.size(); ++j) {
      v[idx] = data[j].values[i - 5];
      idx++;
    }
  }

  ierr = nc_put_var_longlong(ncid, varid_time, t);
  ierr = nc_put_var_double(ncid, varid_data, v);

  delete[] t;
  delete[] v;
  return;
}

void CrmsDatabase::openCrmsFile() {
  this->m_file = std::ifstream(this->m_databaseFile, ios::binary);
}

void CrmsDatabase::closeOutputFile(int ncid, size_t numStations) {
  int ierr = nc_redef(ncid);
  int dimid_nstation;
  ierr = nc_def_dim(ncid, "nstation", numStations, &dimid_nstation);
  ierr = nc_close(ncid);
  return;
}

void CrmsDatabase::closeCrmsFile() {
  if (this->m_file.is_open()) this->m_file.close();
  return;
}

void CrmsDatabase::readHeader() {
  std::string line;
  std::getline(this->m_file, line);
  std::vector<std::string> list = StringUtil::stringSplitToVector(line, ",");
  for (auto &s : list) {
    this->m_dataCategories.push_back(s);
  }
  return;
}

CrmsDatabase::CrmsDataContainer CrmsDatabase::splitToCrmsDataContainer(
    const std::string &line) {
  CrmsDataContainer d;

  std::vector<std::string> split = splitString(line);
  d.id = split[0];

  if (d.id.substr(0, 4) != "CRMS") {
    d.valid = false;
    return d;
  }

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

  d.values.reserve(this->m_dataCategories.size());

  for (size_t i = 5; i < split.size(); ++i) {
    bool ok;
    double v = StringUtil::stringToDouble(split[i], ok);
    if (ok) {
      d.values.push_back(v);
    } else {
      d.values.push_back(this->fillValue());
    }
  }
  d.valid = true;
  return d;
}

bool CrmsDatabase::getNextStation(std::vector<CrmsDataContainer> &data) {
  std::string prevname;
  size_t n = 0;
  bool valid = true;

  data.reserve(300000);

  while (!this->m_file.eof()) {
    std::string line;
    std::getline(this->m_file, line);
    streampos p = this->m_file.tellg();
    CrmsDataContainer d = this->splitToCrmsDataContainer(line);
    if (!d.valid) valid = false;
    if (n == 0) {
      prevname = d.id;
    } else if (prevname != d.id) {
      this->m_file.seekg(p);
      break;
    }
    n++;
    if (valid) data.push_back(d);
  }
  return valid;
}

bool CrmsDatabase::fileExists(const std::string &filename) {
  std::ifstream ifile(filename.c_str());
  return static_cast<bool>(ifile);
}

void CrmsDatabase::initializeOutputFile(int &ncid) {
  int ierr = nc_create(this->m_outputFile.c_str(), NC_NETCDF4, &ncid);
  int dimid_categories, dimid_stringsize, varid_cat;
  ierr = nc_def_dim(ncid, "numParam", this->m_dataCategories.size() - 7,
                    &dimid_categories);
  ierr = nc_def_dim(ncid, "stringsize", 200, &dimid_stringsize);
  int dims[2];
  dims[0] = dimid_categories;
  dims[1] = dimid_stringsize;
  ierr = nc_def_var(ncid, "sensors", NC_CHAR, 2, dims, &varid_cat);
  ierr = nc_enddef(ncid);

  for (size_t i = 5; i < this->m_dataCategories.size() - 2; ++i) {
    std::string s = this->m_dataCategories[i];
    const char *name = s.c_str();
    const size_t start[2] = {i - 5, 0};
    const size_t count[2] = {1, s.length()};
    ierr = nc_put_vara_text(ncid, varid_cat, start, count, name);
  }
  return;
}
