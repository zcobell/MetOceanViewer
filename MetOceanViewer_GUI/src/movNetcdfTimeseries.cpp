#include "movNetcdfTimeseries.h"
#include "netcdf.h"

#define NCCHECK(ierr)     \
  if (ierr != NC_NOERR) { \
    nc_close(ncid);       \
    return ierr;          \
  }

MovNetcdfTimeseries::MovNetcdfTimeseries(QObject *parent) : QObject(parent) {
  this->m_filename = QString();
  this->m_epsg = 4326;
  this->m_units = "unknown";
  this->m_verticalDatum = "unknown";
  this->m_horizontalProjection = "WGS84";
}

QString MovNetcdfTimeseries::filename() const { return this->m_filename; }

void MovNetcdfTimeseries::setFilename(const QString &filename) {
  this->m_filename = filename;
}

int MovNetcdfTimeseries::epsg() const { return m_epsg; }

void MovNetcdfTimeseries::setEpsg(int epsg) { m_epsg = epsg; }

int MovNetcdfTimeseries::read() {
  if (this->m_filename == QString()) return 1;

  QDateTime refTime;
  QString station_dim_string, station_time_var_string, station_data_var_string,
      stationNameString;
  size_t stationNameLength, length;
  int ncid;
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

  NCCHECK(nc_get_var_double(ncid, varid_xcoor, xcoor));
  NCCHECK(nc_get_var_double(ncid, varid_ycoor, ycoor));

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

    NCCHECK(nc_get_var_double(ncid, varid_data, varData));
    NCCHECK(nc_get_var_longlong(ncid, varid_time, timeData));

    this->m_data[i].resize(length);
    this->m_time[i].resize(length);

    for (size_t j = 0; j < length; j++) {
      this->m_data[i][j] = varData[j];
      this->m_time[i][j] = refTime.addSecs(timeData[j]);
    }

    free(timeData);
    free(varData);
  }

  NCCHECK(nc_close(ncid));

  return 0;
}

int MovNetcdfTimeseries::toImeds(MovImeds *imeds) {
  imeds->nstations = this->m_numStations;
  imeds->station.resize(this->m_numStations);
  imeds->datum = "unknown";
  imeds->header1 = "none";
  imeds->header2 = "none";
  imeds->header3 = "none";
  imeds->success = false;

  for (size_t i = 0; i < this->m_numStations; i++) {
    MovImedsStation *station = new MovImedsStation(imeds);
    station->NumSnaps = this->m_time[i].size();
    station->date = this->m_time[i];
    station->data = this->m_data[i];
    station->latitude = this->m_ycoor[i];
    station->longitude = this->m_xcoor[i];
    station->StationName = this->m_stationName[i];
    station->StationID = this->m_stationName[i];
    station->StationIndex = i;

    imeds->station[i] = station;
  }

  imeds->success = true;

  return 0;
}

int MovNetcdfTimeseries::getEpsg(QString file) {
  int ncid, varid_xcoor, epsg;
  NCCHECK(nc_open(file.toStdString().c_str(), NC_NOWRITE, &ncid));
  NCCHECK(nc_inq_varid(ncid, "stationXCoordinate", &varid_xcoor));
  NCCHECK(nc_get_att_int(ncid, varid_xcoor, "HorizontalProjectionEPSG", &epsg));
  NCCHECK(nc_close(ncid));
  return epsg;
}