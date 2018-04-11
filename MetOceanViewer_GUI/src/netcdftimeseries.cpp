#include "netcdftimeseries.h"
#include "netcdf.h"

#define NCCHECK(ierr)     \
  if (ierr != NC_NOERR) { \
    nc_close(ncid);       \
    return ierr;          \
  }

NetcdfTimeseries::NetcdfTimeseries(QObject *parent) : QObject(parent) {
  this->m_filename = QString();
  this->m_epsg = 4326;
  this->m_units = "unknown";
  this->m_verticalDatum = "unknown";
  this->m_horizontalProjection = "WGS84";
  this->m_numStations = 0;
}

QString NetcdfTimeseries::filename() const { return this->m_filename; }

void NetcdfTimeseries::setFilename(const QString &filename) {
  this->m_filename = filename;
}

int NetcdfTimeseries::epsg() const { return m_epsg; }

void NetcdfTimeseries::setEpsg(int epsg) { m_epsg = epsg; }

int NetcdfTimeseries::read() {
  if (this->m_filename == QString()) return 1;

  QDateTime refTime;
  QString station_dim_string, station_time_var_string, station_data_var_string,
      stationNameString;
  size_t stationNameLength, length;
  int ierr, ncid;
  int dimid_nstations, dimidStationLength, dimid_stationNameLen;
  int varid_time, varid_data, varid_xcoor, varid_ycoor, varid_stationName;
  int epsg;
  long long *timeData;
  char timeChar[80];
  char *stationName;
  double *varData, *xcoor, *ycoor;
  const size_t *start, *count;

  NCCHECK(nc_open(this->m_filename.toStdString().c_str(), NC_NOWRITE, &ncid));
  NCCHECK(nc_inq_dimid(ncid, "numStations", &dimid_nstations));
  NCCHECK(nc_inq_dimlen(ncid, dimid_nstations, &this->m_numStations));
  NCCHECK(nc_inq_dimid(ncid, "stationNameLen", &dimid_stationNameLen));
  NCCHECK(nc_inq_dimlen(ncid, dimid_stationNameLen, &stationNameLength));
  NCCHECK(nc_inq_varid(ncid, "stationXCoordinate", &varid_xcoor));
  NCCHECK(nc_inq_varid(ncid, "stationYCoordinate", &varid_ycoor));
  NCCHECK(nc_inq_varid(ncid, "stationName", &varid_stationName));
  NCCHECK(nc_get_att_int(ncid, varid_xcoor, "HorizontalProjectionEPSG", &epsg));

  this->setEpsg(epsg);

  xcoor = (double *)malloc(sizeof(double) * this->m_numStations);
  ycoor = (double *)malloc(sizeof(double) * this->m_numStations);

  ierr = nc_get_var_double(ncid, varid_xcoor, xcoor);
  if (ierr != NC_NOERR) {
    free(xcoor);
    free(ycoor);
    nc_close(ncid);
    return ierr;
  }

  ierr = nc_get_var_double(ncid, varid_ycoor, ycoor);
  if (ierr != NC_NOERR) {
    free(xcoor);
    free(ycoor);
    nc_close(ncid);
    return ierr;
  }

  for (size_t i = 0; i < this->m_numStations; i++) {
    this->m_xcoor.push_back(xcoor[i]);
    this->m_ycoor.push_back(ycoor[i]);
  }

  free(xcoor);
  free(ycoor);

  stationName =
      (char *)malloc(sizeof(char) * stationNameLength * this->m_numStations);

  NCCHECK(nc_get_var_text(ncid, varid_stationName, stationName));
  stationNameString = QString(stationName);

  free(stationName);

  for (size_t i = 0; i < this->m_numStations; i++) {
    this->m_stationName.push_back(
        stationNameString.mid(200 * i, 200).simplified());
  }

  this->m_time.resize(this->m_numStations);
  this->m_data.resize(this->m_numStations);

  for (size_t i = 0; i < this->m_numStations; i++) {
    station_dim_string.sprintf("stationLength_%4.4d", i + 1);
    station_time_var_string.sprintf("time_station_%4.4d", i + 1);
    station_data_var_string.sprintf("data_station_%4.4d", i + 1);

    NCCHECK(nc_inq_dimid(ncid, station_dim_string.toStdString().c_str(),
                         &dimidStationLength));

    NCCHECK(nc_inq_dimlen(ncid, dimidStationLength, &length));
    this->m_stationLength.push_back(length);

    NCCHECK(nc_inq_varid(ncid, station_time_var_string.toStdString().c_str(),
                         &varid_time));

    NCCHECK(nc_inq_varid(ncid, station_data_var_string.toStdString().c_str(),
                         &varid_data));

    NCCHECK(nc_get_att_text(ncid, varid_time, "referenceDate", timeChar));
    QString timeString = QString(timeChar).mid(0, 19);
    refTime = QDateTime::fromString(timeString, "yyyy-MM-dd hh:mm:ss");
    refTime.setTimeSpec(Qt::UTC);

    timeData = (long long *)malloc(sizeof(long long) * length);
    varData = (double *)malloc(sizeof(double) * length);

    start = 0;
    count = &length;

    ierr = nc_get_var_double(ncid, varid_data, varData);
    if (ierr != NC_NOERR) {
      free(timeData);
      free(varData);
      nc_close(ncid);
      return ierr;
    }

    ierr = nc_get_var_longlong(ncid, varid_time, timeData);
    if (ierr != NC_NOERR) {
      free(timeData);
      free(varData);
      nc_close(ncid);
      return ierr;
    }

    this->m_data[i].resize(length);
    this->m_time[i].resize(length);

    for (size_t j = 0; j < length; j++) {
      this->m_data[i][j] = varData[j];
      this->m_time[i][j] = refTime.addSecs(timeData[j]).toMSecsSinceEpoch();
    }

    free(timeData);
    free(varData);
  }

  NCCHECK(nc_close(ncid));

  return 0;
}

int NetcdfTimeseries::toImeds(Imeds *imeds) {
  imeds->nstations = this->m_numStations;
  imeds->station.resize(this->m_numStations);
  imeds->datum = "unknown";
  imeds->header1 = "none";
  imeds->header2 = "none";
  imeds->header3 = "none";
  imeds->success = false;

  for (size_t i = 0; i < this->m_numStations; i++) {
    imeds->station[i].NumSnaps = this->m_time[i].size();
    imeds->station[i].date = this->m_time[i];
    imeds->station[i].data = this->m_data[i];
    imeds->station[i].latitude = this->m_ycoor[i];
    imeds->station[i].longitude = this->m_xcoor[i];
    imeds->station[i].StationName = this->m_stationName[i];
    imeds->station[i].StationID = this->m_stationName[i];
    imeds->station[i].StationIndex = i;
  }

  imeds->success = true;

  return 0;
}

int NetcdfTimeseries::getEpsg(QString file) {
  int ncid, varid_xcoor, epsg;
  NCCHECK(nc_open(file.toStdString().c_str(), NC_NOWRITE, &ncid));
  NCCHECK(nc_inq_varid(ncid, "stationXCoordinate", &varid_xcoor));
  NCCHECK(nc_get_att_int(ncid, varid_xcoor, "HorizontalProjectionEPSG", &epsg));
  NCCHECK(nc_close(ncid));
  return epsg;
}
