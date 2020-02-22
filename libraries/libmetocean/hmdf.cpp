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
#include "hmdf.h"
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <fstream>
#include "hmdfasciiparser.h"
#include "netcdf.h"
#include "netcdftimeseries.h"
#include "stringutil.h"

#define NCCHECK(ierr)     \
  if (ierr != NC_NOERR) { \
    nc_close(ncid);       \
    return ierr;          \
  }

Hmdf::Hmdf(QObject *parent) : QObject(parent) { this->init(); }

void Hmdf::init() {
  this->setHeader1("");
  this->setHeader2("");
  this->setHeader3("");
  this->setDatum("");
  this->setSuccess(false);
  this->setUnits("");
  this->setNull(true);
  return;
}

void Hmdf::clear() {
  for (int i = 0; i < this->m_station.size(); i++) {
    this->m_station[i]->clear();
  }
  this->init();
  return;
}

size_t Hmdf::nstations() const { return this->m_station.size(); }

QString Hmdf::header1() const { return this->m_header1; }

void Hmdf::setHeader1(const QString &header1) { this->m_header1 = header1; }

QString Hmdf::header2() const { return this->m_header2; }

void Hmdf::setHeader2(const QString &header2) { this->m_header2 = header2; }

QString Hmdf::header3() const { return this->m_header3; }

void Hmdf::setHeader3(const QString &header3) { this->m_header3 = header3; }

QString Hmdf::units() const { return this->m_units; }

void Hmdf::setUnits(const QString &units) { this->m_units = units; }

QString Hmdf::datum() const { return this->m_datum; }

void Hmdf::setDatum(const QString &datum) { this->m_datum = datum; }

HmdfStation *Hmdf::station(int index) {
  Q_ASSERT(index >= 0 && index < this->m_station.size());
  return this->m_station[index];
}

void Hmdf::setStation(int index, HmdfStation *station) {
  Q_ASSERT(index >= 0 && index < this->m_station.size());
  this->m_station[index] = station;
}

void Hmdf::addStation(HmdfStation *station) {
  if (station->parent() != this) {
    station->setParent(this);  //...Take ownership over
  }
  this->m_station.push_back(station);
}

bool Hmdf::success() const { return this->m_success; }

void Hmdf::setSuccess(bool success) { this->m_success = success; }

bool Hmdf::null() const { return this->m_null; }

void Hmdf::setNull(bool null) { this->m_null = null; }

int Hmdf::readImeds(QString filename) {
  std::fstream fid(filename.toStdString().c_str());
  if (fid.bad()) return -1;

  //...Read Header
  std::string templine;
  std::getline(fid, templine);
  this->m_header1 =
      QString::fromStdString(StringUtil::sanitizeString(templine));
  std::getline(fid, templine);
  this->m_header2 =
      QString::fromStdString(StringUtil::sanitizeString(templine));
  std::getline(fid, templine);
  this->m_header3 =
      QString::fromStdString(StringUtil::sanitizeString(templine));

  //...Read Body
  std::getline(fid, templine);

  while (!fid.eof()) {
    HmdfStation *station = new HmdfStation(this);

    templine = StringUtil::sanitizeString(templine);

    QStringList templist =
        QString::fromStdString(templine).split(" ", QString::SkipEmptyParts);

    station->setName(templist.at(0));
    station->setLongitude(templist.at(2).toDouble());
    station->setLatitude(templist.at(1).toDouble());

    while (!fid.eof()) {
      std::getline(fid, templine);

      int year, month, day, hour, minute, second;
      double value;

      bool status = HmdfAsciiParser::splitStringHmdfFormat(
          templine, year, month, day, hour, minute, second, value);

      if (status) {
        QDateTime datetime;
        datetime.setTimeSpec(Qt::UTC);
        datetime.setDate(QDate(year, month, day));
        datetime.setTime(QTime(hour, minute, second));
        qint64 secs = datetime.toMSecsSinceEpoch();

        //...Append to the station data
        station->setNext(secs, value);
      } else {
        break;
      }
    }

    //...Add the station
    this->addStation(station);
  }

  this->setNull(false);

  return 0;
}

int Hmdf::readNetcdf(QString filename) {
  NetcdfTimeseries *ncts = new NetcdfTimeseries(this);
  ncts->setFilename(filename);
  int ierr = ncts->read();
  if (ierr != 0) {
    delete ncts;
    return 1;
  }
  ierr = ncts->toHmdf(this);
  delete ncts;

  if (ierr != 0) return 1;

  this->setNull(false);

  return 0;
}

int Hmdf::writeCsv(QString filename) {
  int i, s;
  QString value;
  QFile output(filename);

  if (!output.open(QIODevice::WriteOnly)) return -1;

  for (s = 0; s < this->nstations(); s++) {
    output.write(
        QString("Station: " + this->station(s)->name() + "\n").toUtf8());
    output.write(QString("Datum: " + this->datum() + "\n").toUtf8());
    output.write(QString("Units: " + this->units() + "\n").toUtf8());
    output.write(QString("\n").toUtf8());
    for (i = 0; i < this->station(s)->numSnaps(); i++) {
      QDateTime d =
          QDateTime::fromMSecsSinceEpoch(this->station(s)->date(i), Qt::UTC);
      if (d.isValid()) {
        value.sprintf("%10.4e", this->station(s)->data(i));
        output.write(
            QString(d.toString("MM/dd/yyyy,hh:mm,") + value + "\n").toUtf8());
      }
    }
    output.write(QString("\n\n\n").toUtf8());
  }
  output.close();
  return 0;
}

int Hmdf::writeImeds(QString filename) {
  QString value;
  QFile outputFile(filename);

  if (!outputFile.open(QIODevice::WriteOnly)) return -1;

  outputFile.write(QString("% IMEDS generic format\n").toUtf8());
  outputFile.write(QString("% year month day hour min sec value\n").toUtf8());
  outputFile.write(QString("MetOceanViewer    UTC    " + this->datum() + "   " +
                           this->units() + "\n")
                       .toUtf8());

  for (int s = 0; s < this->nstations(); s++) {
    QString stationName =
        this->station(s)->name().replace(" ", "_").replace(",", "_").replace(
            "__", "_");
    outputFile.write(
        QString(stationName + "   " +
                QString::number(this->station(s)->latitude()) + "   " +
                QString::number(this->station(s)->longitude()) + "\n")
            .toUtf8());

    for (int i = 0; i < this->station(s)->numSnaps(); i++) {
      QDateTime d =
          QDateTime::fromMSecsSinceEpoch(this->station(s)->date(i), Qt::UTC);

      if (d.isValid()) {
        value.sprintf("%10.4e", this->station(s)->data(i));
        outputFile.write(
            QString(d.toString("yyyy    MM    dd    hh    mm    ss") + "    " +
                    value + "\n")
                .toUtf8());
      }
    }
  }
  outputFile.close();
  return 0;
}

void Hmdf::deallocNcArrays(long long *time, double *data, char *name,
                           char *id) {
  delete[] time;
  delete[] data;
  delete[] name;
  delete[] id;
  return;
}

int Hmdf::writeNetcdf(QString filename) {
  int ncid;
  int dimid_nstations, dimid_stationNameLength;
  int varid_stationName, varid_stationx, varid_stationy;
  int varid_stationId;

  QVector<int> dimid_stationLength;
  QVector<int> varid_stationDate, varid_stationData;

  //...Open file
  NCCHECK(nc_create(filename.toStdString().c_str(), NC_NETCDF4, &ncid));

  //...Dimensions
  NCCHECK(nc_def_dim(ncid, "numStations", this->nstations(), &dimid_nstations));
  NCCHECK(nc_def_dim(ncid, "stationNameLen", 200, &dimid_stationNameLength));
  for (int i = 0; i < this->nstations(); i++) {
    QString dimname;
    int d;
    dimname.sprintf("%s%4.4i", "stationLength_", i + 1);
    NCCHECK(nc_def_dim(ncid, dimname.toStdString().c_str(),
                       this->station(i)->numSnaps(), &d));
    dimid_stationLength.push_back(d);
  }

  //...Variables
  int stationNameDims[2] = {dimid_nstations, dimid_stationNameLength};
  int nstationDims[1] = {dimid_nstations};
  int wgs84[1] = {4326};

  NCCHECK(nc_def_var(ncid, "stationName", NC_CHAR, 2, stationNameDims,
                     &varid_stationName));
  NCCHECK(nc_def_var(ncid, "stationId", NC_CHAR, 2, stationNameDims,
                     &varid_stationId));
  NCCHECK(nc_def_var(ncid, "stationXCoordinate", NC_DOUBLE, 1, nstationDims,
                     &varid_stationx));
  NCCHECK(nc_def_var(ncid, "stationYCoordinate", NC_DOUBLE, 1, nstationDims,
                     &varid_stationy));

  NCCHECK(nc_put_att_text(ncid, varid_stationx, "HorizontalProjectionName", 5,
                          "WGS84"));
  NCCHECK(nc_put_att_text(ncid, varid_stationy, "HorizontalProjectionName", 5,
                          "WGS84"));

  NCCHECK(nc_put_att_int(ncid, varid_stationx, "HorizontalProjectionEPSG",
                         NC_INT, 1, wgs84));
  NCCHECK(nc_put_att_int(ncid, varid_stationy, "HorizontalProjectionEPSG",
                         NC_INT, 1, wgs84));

  for (int i = 0; i < this->nstations(); i++) {
    QString stationName, timeVarName, dataVarName;
    int d[1] = {dimid_stationLength[i]};
    char epoch[20] = "1970-01-01 00:00:00";
    char utc[4] = "utc";
    char timeunit[27] = "second since referenceDate";
    int v;

    stationName.sprintf("%s%4.4i", "station_", i + 1);
    timeVarName = "time_" + stationName;
    dataVarName = "data_" + stationName;

    NCCHECK(nc_def_var(ncid, timeVarName.toStdString().c_str(), NC_INT64, 1, d,
                       &v));
    NCCHECK(nc_put_att_text(ncid, v, "StationName",
                            this->station(i)->name().length(),
                            this->station(i)->name().toStdString().c_str()));
    NCCHECK(nc_put_att_text(ncid, v, "StationID",
                            this->station(i)->id().length(),
                            this->station(i)->id().toStdString().c_str()));
    NCCHECK(nc_put_att_text(ncid, v, "referenceDate", 20, epoch));
    NCCHECK(nc_put_att_text(ncid, v, "timezone", 3, utc));
    NCCHECK(nc_put_att_text(ncid, v, "units", 3, timeunit));
    NCCHECK(nc_def_var_deflate(ncid, v, 1, 1, 2));
    varid_stationDate.push_back(v);

    NCCHECK(nc_def_var(ncid, dataVarName.toStdString().c_str(), NC_DOUBLE, 1, d,
                       &v));
    NCCHECK(nc_put_att_text(ncid, v, "StationName",
                            this->station(i)->name().length(),
                            this->station(i)->name().toStdString().c_str()));
    NCCHECK(nc_put_att_text(ncid, v, "StationID",
                            this->station(i)->id().length(),
                            this->station(i)->id().toStdString().c_str()));
    NCCHECK(nc_put_att_text(ncid, v, "units", this->units().length(),
                            this->units().toStdString().c_str()));
    NCCHECK(nc_put_att_text(ncid, v, "datum", this->datum().length(),
                            this->datum().toStdString().c_str()));
    NCCHECK(nc_def_var_deflate(ncid, v, 1, 1, 2));
    varid_stationData.push_back(v);
  }

  //...Metadata
  QString name = qgetenv("USER");
  if (name.isEmpty()) name = qgetenv("USERNAME");
  QString host = QHostInfo::localHostName();
  QString createTime =
      QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd hh:mm:ss");
  QString source = "MetOceanViewer";
  QString ncVersion = QString(nc_inq_libvers());
  QString format = "20180123";

  NCCHECK(nc_put_att(ncid, NC_GLOBAL, "source", NC_CHAR, source.length(),
                     source.toStdString().c_str()));
  NCCHECK(nc_put_att(ncid, NC_GLOBAL, "creation_date", NC_CHAR,
                     createTime.length(), createTime.toStdString().c_str()));
  NCCHECK(nc_put_att(ncid, NC_GLOBAL, "created_by", NC_CHAR, name.length(),
                     name.toStdString().c_str()));
  NCCHECK(nc_put_att(ncid, NC_GLOBAL, "host", NC_CHAR, host.length(),
                     host.toStdString().c_str()));
  NCCHECK(nc_put_att(ncid, NC_GLOBAL, "netCDF_version", NC_CHAR,
                     ncVersion.length(), ncVersion.toStdString().c_str()));
  NCCHECK(nc_put_att(ncid, NC_GLOBAL, "fileformat", NC_CHAR, format.length(),
                     format.toStdString().c_str()));

  NCCHECK(nc_enddef(ncid));

  for (size_t i = 0; i < this->nstations(); i++) {
    size_t index[2] = {i, 0};
    size_t stindex[1] = {i};
    size_t count[2] = {1, 200};
    double lat[1] = {this->station(i)->latitude()};
    double lon[1] = {this->station(i)->longitude()};

    long long *time = new long long[this->station(i)->numSnaps()];
    double *data = new double[this->station(i)->numSnaps()];
    char *name = new char[200];
    char *id = new char[200];

    memset(name, ' ', 200);
    memset(id, ' ', 200);

    this->station(i)->name().toStdString().copy(
        name, this->station(i)->name().length(), 0);
    this->station(i)->id().toStdString().copy(
        id, this->station(i)->id().length(), 0);

    for (int j = 0; j < this->station(i)->numSnaps(); j++) {
      time[j] = this->station(i)->date(j) / 1000;
      data[j] = this->station(i)->data(j);
    }

    int status = nc_put_var1_double(ncid, varid_stationx, stindex, lon);
    if (status != NC_NOERR) {
      this->deallocNcArrays(time, data, name, id);
      nc_close(ncid);
      return status;
    }

    status = nc_put_var1_double(ncid, varid_stationy, stindex, lat);
    if (status != NC_NOERR) {
      this->deallocNcArrays(time, data, name, id);
      nc_close(ncid);
      return status;
    }

    status = nc_put_var_longlong(ncid, varid_stationDate[i], time);
    if (status != NC_NOERR) {
      this->deallocNcArrays(time, data, name, id);
      nc_close(ncid);
      return status;
    }

    status = nc_put_var_double(ncid, varid_stationData[i], data);
    if (status != NC_NOERR) {
      this->deallocNcArrays(time, data, name, id);
      nc_close(ncid);
      return status;
    }

    status = nc_put_vara_text(ncid, varid_stationName, index, count, name);
    if (status != NC_NOERR) {
      this->deallocNcArrays(time, data, name, id);
      nc_close(ncid);
      return status;
    }

    status = nc_put_vara_text(ncid, varid_stationId, index, count, id);
    if (status != NC_NOERR) {
      this->deallocNcArrays(time, data, name, id);
      nc_close(ncid);
      return status;
    }

    ;
    this->deallocNcArrays(time, data, name, id);
  }

  nc_close(ncid);

  return 0;
}

int Hmdf::write(QString filename, HmdfFileType fileType) {
  if (fileType == HmdfImeds) {
    return this->writeImeds(filename);
  } else if (fileType == HmdfCsv) {
    return this->writeCsv(filename);
  } else if (fileType == HmdfNetCdf) {
    return this->writeNetcdf(filename);
  }
  return 1;
}

int Hmdf::write(QString filename) {
  QFileInfo info(filename);
  if (info.suffix().toLower() == "imeds") {
    return this->write(filename, HmdfImeds);
  } else if (info.suffix().toLower() == "csv") {
    return this->write(filename, HmdfCsv);
  } else if (info.suffix().toLower() == "nc") {
    return this->write(filename, HmdfNetCdf);
  }
  return 1;
}

void Hmdf::dataBounds(qint64 &dateMin, qint64 &dateMax, double &minValue,
                      double &maxValue) {
  dateMax = std::numeric_limits<qint64>::max();
  dateMin = -std::numeric_limits<qint64>::max();
  maxValue = -std::numeric_limits<double>::max();
  minValue = std::numeric_limits<double>::max();

  for (size_t i = 0; i < this->nstations(); ++i) {
    qint64 tempDateMin, tempDateMax;
    double tempMinValue, tempMaxValue;
    if (!this->station(i)->isNull()) {
      this->station(i)->dataBounds(tempDateMin, tempDateMax, tempMinValue,
                                   tempMaxValue);
      dateMin = std::min(tempDateMin, dateMin);
      dateMax = std::max(tempDateMax, dateMax);
      minValue = std::min(tempMinValue, minValue);
      maxValue = std::max(tempMaxValue, maxValue);
    }
  }
  return;
}

bool Hmdf::applyDatumCorrection(const Station &s, const Datum::VDatum &datum) {
  int ierr = 0;
  for (auto &stn : this->m_station) {
    ierr += stn->applyDatumCorrection(s, datum);
  }
  if (ierr != 0) return false;
  this->setDatum(datumName(datum));
  return true;
}

bool Hmdf::applyDatumCorrection(QVector<Station> &s, Datum::VDatum datum) {
  int ierr = 0;
  for (size_t i = 0; i < this->nstations(); ++i) {
    ierr += this->m_station[i]->applyDatumCorrection(s[i], datum);
  }
  if (ierr != 0) return false;
  this->setDatum(Datum::datumName(datum));
  return true;
}
