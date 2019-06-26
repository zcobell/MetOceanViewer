#include "crmsdata.h"
#include <QDebug>
#include <QFileInfo>
#include "netcdf.h"

CrmsData::CrmsData(Station &station, QDateTime startDate, QDateTime endDate,
                   QObject *parent)
    : WaterData(station, startDate, endDate, parent) {}

int CrmsData::retrieveData(Hmdf *data) { return 0; }

bool CrmsData::readStationList(const QString &filename,
                               QVector<double> &latitude,
                               QVector<double> &longitude,
                               QVector<QString> &stationNames) {
  int ncid, dimid_nstation, dimid_stringsize;
  size_t nstations, stringsize;
  int ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  ierr = nc_inq_dimid(ncid, "nstation", &dimid_nstation);
  ierr = nc_inq_dimlen(ncid, dimid_nstation, &nstations);
  ierr = nc_inq_dimid(ncid, "stringsize", &dimid_stringsize);
  ierr = nc_inq_dimlen(ncid, dimid_stringsize, &stringsize);

  longitude.reserve(nstations);
  latitude.reserve(nstations);
  stationNames.reserve(nstations);

  for (size_t i = 0; i < nstations; ++i) {
    int varid;
    double lat, lon;
    char *nm = new char[stringsize];
    QString stationDataString;
    stationDataString.sprintf("data_station_%4.4llu", i + 1);
    ierr = nc_inq_varid(ncid, stationDataString.toStdString().c_str(), &varid);
    ierr = nc_get_att_double(ncid, varid, "latitude", &lat);
    ierr = nc_get_att_double(ncid, varid, "longitude", &lon);
    ierr = nc_get_att_text(ncid, varid, "station_name", nm);
    QString name(nm);
    latitude.push_back(lat);
    longitude.push_back(lon);
    stationNames.push_back(name);
    delete[] nm;
  }
  nc_close(ncid);
  return true;
}

bool CrmsData::inquireCrmsStatus(QString filename) {
  QFileInfo f(filename);
  return f.exists();
}
