#include "dflow.h"
#include <QtMath>
#include "boost/algorithm/string.hpp"
#include "errors.h"
#include "hmdf.h"
#include "metoceanviewer.h"
#include "netcdf.h"

Dflow::Dflow(QString filename, QObject *parent) : QObject(parent) {
  this->_isInitialized = false;
  this->_readError = true;
  this->_filename = filename;
  this->_is3d = false;
  this->error = new Errors(this);
  this->_nSteps = 0;
  this->_nStations = 0;
  this->_nLayers = 0;

  int ierr = this->_init();

  this->error->setErrorCode(ierr);

  if (this->error->isError()) {
    this->_isInitialized = true;
    this->_readError = false;
  } else {
    this->_isInitialized = false;
    this->_readError = true;
  }
  return;
}

bool Dflow::is3d() { return this->_is3d; }

QStringList Dflow::getVaribleList() { return QStringList(this->_plotvarnames); }

int Dflow::getNumLayers() { return this->_nLayers; }

bool Dflow::variableIs3d(QString variable) {
  if (variable == "velocity_magnitude") {
    if (this->_nDims["x_velocity"] == 3)
      return true;
    else
      return false;
  }
  if (this->_nDims[variable] == 3)
    return true;
  else
    return false;
}

int Dflow::_get2DVelocityMagnitude(int layer, QVector<QVector<double>> &data) {
  int ierr, i, j;
  QVector<QVector<double>> x_data, y_data;

  ierr = this->_getVar(QStringLiteral("x_velocity"), layer, x_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOXVELOCITY);
    return this->error->errorCode();
  }

  ierr = this->_getVar(QStringLiteral("y_velocity"), layer, y_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOYVELOCITY);
    return this->error->errorCode();
  }

  data.resize(this->_nStations);
  for (i = 0; i < this->_nStations; i++) {
    data[i].resize(this->_nSteps);
    for (j = 0; j < this->_nSteps; j++) {
      if (x_data[i][j] == HmdfStation::nullDataValue() ||
          y_data[i][j] == HmdfStation::nullDataValue())
        data[i][j] = HmdfStation::nullDataValue();
      else
        data[i][j] = qSqrt(qPow(x_data[i][j], 2.0) + qPow(y_data[i][j], 2.0));
    }
  }
  return MetOceanViewer::Error::NOERR;
}

int Dflow::_get2DVelocityDirection(int layer, QVector<QVector<double>> &data) {
  int ierr, i, j;
  QVector<QVector<double>> x_data, y_data;

  ierr = this->_getVar(QStringLiteral("x_velocity"), layer, x_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOXVELOCITY);
    return this->error->errorCode();
  }

  ierr = this->_getVar(QStringLiteral("y_velocity"), layer, y_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOYVELOCITY);
    return this->error->errorCode();
  }

  data.resize(this->_nStations);
  for (i = 0; i < this->_nStations; i++) {
    data[i].resize(this->_nSteps);
    for (j = 0; j < this->_nSteps; j++)
      if (x_data[i][j] == HmdfStation::nullDataValue() ||
          y_data[i][j] == HmdfStation::nullDataValue())
        data[i][j] = HmdfStation::nullDataValue();
      else
        data[i][j] = qAtan2(y_data[i][j], x_data[i][j]) * 180.0 / M_PI;
  }
  return MetOceanViewer::Error::NOERR;
}

int Dflow::_get3DVeloctiyMagnitude(int layer, QVector<QVector<double>> &data) {
  int ierr, i, j;
  QVector<QVector<double>> x_data, y_data, z_data;

  ierr = this->_getVar(QStringLiteral("x_velocity"), layer, x_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOXVELOCITY);
    return this->error->errorCode();
  }

  ierr = this->_getVar(QStringLiteral("y_velocity"), layer, y_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOYVELOCITY);
    return this->error->errorCode();
  }

  ierr = this->_getVar(QStringLiteral("z_velocity"), layer, z_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOZVELOCITY);
    return this->error->errorCode();
  }

  data.resize(this->_nStations);
  for (i = 0; i < this->_nStations; i++) {
    data[i].resize(this->_nSteps);
    for (j = 0; j < this->_nSteps; j++) {
      if (x_data[i][j] == HmdfStation::nullDataValue() ||
          y_data[i][j] == HmdfStation::nullDataValue() ||
          z_data[i][j] == HmdfStation::nullDataValue())
        data[i][j] = HmdfStation::nullDataValue();
      else
        data[i][j] = qSqrt(qPow(x_data[i][j], 2.0) + qPow(y_data[i][j], 2.0) +
                           qPow(z_data[i][j], 2.0));
    }
  }
  return MetOceanViewer::Error::NOERR;
}

int Dflow::_getWindVelocityMagnitude(QVector<QVector<double>> &data) {
  int ierr, i, j;
  QVector<QVector<double>> x_data, y_data;

  ierr = this->_getVar(QStringLiteral("windx"), 0, x_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOXVELOCITY);
    return this->error->errorCode();
  }
  ierr = this->_getVar(QStringLiteral("windy"), 0, y_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOYVELOCITY);
    return this->error->errorCode();
  }
  data.resize(this->_nStations);
  for (i = 0; i < this->_nStations; i++) {
    data[i].resize(this->_nSteps);
    for (j = 0; j < this->_nSteps; j++) {
      if (x_data[i][j] == HmdfStation::nullDataValue() ||
          y_data[i][j] == HmdfStation::nullDataValue())
        data[i][j] = HmdfStation::nullDataValue();
      else
        data[i][j] = qSqrt(qPow(x_data[i][j], 2.0) + qPow(y_data[i][j], 2.0));
    }
  }
  return MetOceanViewer::Error::NOERR;
}

int Dflow::_getWindDirection(QVector<QVector<double>> &data) {
  int ierr, i, j;
  QVector<QVector<double>> x_data, y_data;

  ierr = this->_getVar(QStringLiteral("windx"), 0, x_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOXVELOCITY);
    return this->error->errorCode();
  }
  ierr = this->_getVar(QStringLiteral("windy"), 0, y_data);
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_NOYVELOCITY);
    return this->error->errorCode();
  }
  data.resize(this->_nStations);
  for (i = 0; i < this->_nStations; i++) {
    data[i].resize(this->_nSteps);
    for (j = 0; j < this->_nSteps; j++)
      if (x_data[i][j] == HmdfStation::nullDataValue() ||
          y_data[i][j] == HmdfStation::nullDataValue())
        data[i][j] = HmdfStation::nullDataValue();
      else
        data[i][j] = qAtan2(y_data[i][j], x_data[i][j]) * 180.0 / M_PI;
  }
  return MetOceanViewer::Error::NOERR;
}

int Dflow::getVariable(QString variable, int layer, Hmdf *hmdf) {
  int i, ierr;
  QVector<qint64> time;
  QVector<QVector<double>> data;

  ierr = this->_getTime(time);
  this->error->setErrorCode(ierr);
  if (this->error->isError()) return this->error->errorCode();

  //...Check for derrived data or just retrieve the
  //   requested variable
  if (variable == QStringLiteral("2D_current_speed"))
    ierr = this->_get2DVelocityMagnitude(layer, data);
  else if (variable == QStringLiteral("2D_current_direction"))
    ierr = this->_get2DVelocityDirection(layer, data);
  else if (variable == QStringLiteral("3D_current_speed"))
    ierr = this->_get3DVeloctiyMagnitude(layer, data);
  else if (variable == QStringLiteral("wind_speed"))
    ierr = this->_getWindVelocityMagnitude(data);
  else if (variable == QStringLiteral("wind_direction"))
    ierr = this->_getWindDirection(data);
  else
    ierr = this->_getVar(variable, layer, data);

  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(ierr);
    return this->error->errorCode();
  }

  hmdf->setSuccess(false);
  hmdf->setDatum("dflowfm_datum");
  hmdf->setHeader1("DFlowFM");
  hmdf->setHeader2("DFlowFM");
  hmdf->setHeader3("DFlowFM");

  for (i = 0; i < this->_nStations; i++) {
    HmdfStation *station = new HmdfStation(hmdf);
    station->setDate(time);
    station->setData(data[i]);
    station->setLatitude(this->_yCoordinates[i]);
    station->setLongitude(this->_xCoordinates[i]);
    station->setStationIndex(i);
    station->setId(QString::number(i));
    station->setName(this->_stationNames[i]);
    hmdf->addStation(station);
  }
  hmdf->setSuccess(true);

  return MetOceanViewer::Error::NOERR;
}

int Dflow::_init() {
  int ierr;

  ierr = this->_getPlottingVariables();
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_GETPLOTVARS);
    return this->error->errorCode();
  }

  ierr = this->_getStations();
  if (ierr != MetOceanViewer::Error::NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::DFLOW_GETSTATIONS);
    return this->error->errorCode();
  }

  return MetOceanViewer::Error::NOERR;
}

int Dflow::_get3d() {
  int ierr, ncid;
  size_t nLayers;

  if (this->_dimnames.contains("laydimw"))
    this->_is3d = true;
  else {
    this->_is3d = false;
    return MetOceanViewer::Error::NOERR;
  }

  ierr = nc_open(this->_filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  ierr = nc_inq_dimlen(ncid, this->_dimnames["laydim"], &nLayers);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  ierr = nc_close(ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  this->_nLayers = (int)nLayers;

  return MetOceanViewer::Error::NOERR;
}

#include <iostream>
int Dflow::_getPlottingVariables() {
  int ncid;
  int ierr = nc_open(this->_filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  this->error->setNcErrorCode(ierr);
  if (this->error->isNcError()) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    return this->error->errorCode();
  }

  int nvar;
  ierr = nc_inq_nvars(ncid, &nvar);
  this->error->setNcErrorCode(ierr);
  if (this->error->isNcError()) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    ierr = nc_close(ncid);
    return this->error->errorCode();
  }

  int ndim;
  ierr = nc_inq_ndims(ncid, &ndim);
  this->error->setNcErrorCode(ierr);
  if (this->error->isNcError()) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    ierr = nc_close(ncid);
    return this->error->errorCode();
  }

  std::vector<int> dims(NC_MAX_DIMS);

  for (auto i = 0; i < ndim; i++) {
    std::string varname(NC_MAX_NAME + 1, ' ');
    ierr = nc_inq_dimname(ncid, i, &varname[0]);
    if (ierr != NC_NOERR) {
      this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
      this->error->setNcErrorCode(ierr);
      ierr = nc_close(ncid);
      return MetOceanViewer::Error::NETCDF;
    }
    boost::trim_right(varname);
    varname.erase(std::find(varname.begin(), varname.end(), '\0'),
                  varname.end());
    auto sname = QByteArray::fromStdString(varname);
    this->_dimnames[sname] = i;
  }

  for (auto i = 0; i < nvar; i++) {
    std::string varname(NC_MAX_NAME + 1, ' ');
    ierr = nc_inq_varname(ncid, i, &varname[0]);
    if (ierr != NC_NOERR) {
      this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
      this->error->setNcErrorCode(ierr);
      ierr = nc_close(ncid);
      return MetOceanViewer::Error::NETCDF;
    }

    boost::trim_right(varname);
    varname.erase(std::find(varname.begin(), varname.end(), '\0'),
                  varname.end());
    auto sname = QString::fromStdString(varname);

    int nd;
    ierr = nc_inq_varndims(ncid, i, &nd);
    if (ierr != NC_NOERR) {
      this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
      this->error->setNcErrorCode(ierr);
      ierr = nc_close(ncid);
      return MetOceanViewer::Error::NETCDF;
    }

    this->_nDims[sname] = nd;

    ierr = nc_inq_vardimid(ncid, i, dims.data());
    if (ierr != NC_NOERR) {
      this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
      this->error->setNcErrorCode(ierr);
      ierr = nc_close(ncid);
      return MetOceanViewer::Error::NETCDF;
    }

    if (nd == 2) {
      if (dims[0] == this->_dimnames["time"] &&
          dims[1] == this->_dimnames["stations"])
        this->_plotvarnames.append(sname);
    } else if (nd == 3) {
      if (dims[0] == this->_dimnames["time"] &&
          dims[1] == this->_dimnames["stations"] &&
          dims[2] == this->_dimnames["laydim"])
        this->_plotvarnames.append(sname);
      //            else if(dims[0]==this->_dimnames["time"] &&
      //                    dims[1]==this->_dimnames["stations"] &&
      //                    dims[2]==this->_dimnames["laydimw"])
      //                this->_plotvarnames.append(sname);
    }
    this->_varnames[sname] = i;
  }

  ierr = nc_close(ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  ierr = this->_get3d();
  if (ierr != MetOceanViewer::Error::NOERR)
    return MetOceanViewer::Error::DFLOW_3DVARS;

  if (this->is3d()) {
    if (this->_plotvarnames.contains("x_velocity") &&
        this->_plotvarnames.contains("y_velocity") &&
        this->_plotvarnames.contains("z_velocity"))
      this->_plotvarnames.append("3D_current_speed");
  }

  if (this->_plotvarnames.contains("x_velocity") &&
      this->_plotvarnames.contains("y_velocity")) {
    this->_plotvarnames.append("2D_current_speed");
    this->_plotvarnames.append("2D_current_direction");
  }

  if (this->_plotvarnames.contains("windx") &&
      this->_plotvarnames.contains("windy")) {
    this->_plotvarnames.append("wind_speed");
    this->_plotvarnames.append("wind_direction");
  }

  return MetOceanViewer::Error::NOERR;
}

int Dflow::_getStations() {
  size_t nstation, name_len;

  int ncid, varid_xcoor, varid_ycoor, varid_namevar;
  int dimid_nsta, dimid_namelen;
  int ierr = nc_open(this->_filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != 0) {
    return MetOceanViewer::Error::CANNOT_OPEN_FILE;
  }

  ierr += nc_inq_dimid(ncid, "stations", &dimid_nsta);
  ierr += nc_inq_dimid(ncid, "name_len", &dimid_namelen);
  ierr += nc_inq_varid(ncid, "station_x_coordinate", &varid_xcoor);
  ierr += nc_inq_varid(ncid, "station_y_coordinate", &varid_ycoor);
  ierr += nc_inq_varid(ncid, "station_name", &varid_namevar);
  if (ierr != 0) {
    return MetOceanViewer::Error::DFLOW_FILEREADERROR;
  }

  ierr += nc_inq_dimlen(ncid, dimid_nsta, &nstation);
  ierr += nc_inq_dimlen(ncid, dimid_namelen, &name_len);
  if (ierr != 0) {
    return MetOceanViewer::Error::DFLOW_FILEREADERROR;
  }

  this->_nStations = nstation;

  std::string stationName(name_len * nstation, ' ');
  this->_xCoordinates.resize(this->_nStations);
  this->_yCoordinates.resize(this->_nStations);
  this->_stationNames.resize(this->_nStations);

  ierr += nc_get_var_text(ncid, varid_namevar, &stationName[0]);
  if (ierr != 0) {
    return MetOceanViewer::Error::DFLOW_FILEREADERROR;
  }

  for (size_t i = 0; i < nstation; i++) {
    QString s = QByteArray::fromStdString(stationName.substr(200 * i, 200))
                    .simplified();
    this->_stationNames[i] = s;
  }

  ierr += nc_get_var(ncid, varid_xcoor, this->_xCoordinates.data());
  ierr += nc_get_var(ncid, varid_ycoor, this->_yCoordinates.data());
  if (ierr != 0) {
    return MetOceanViewer::Error::DFLOW_FILEREADERROR;
  }

  nc_close(ncid);
  return 0;
}

int Dflow::_getTime(QVector<qint64> &timeList) {
  int varid_time = this->_varnames["time"];
  int dimid_time = this->_dimnames["time"];
  std::string units = "units";

  int ncid;
  int ierr = nc_open(this->_filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  size_t nsteps;
  ierr = nc_inq_dimlen(ncid, dimid_time, &nsteps);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  size_t unitsLen;
  ierr = nc_inq_attlen(ncid, varid_time, units.data(), &unitsLen);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  std::string refstring(unitsLen, ' ');
  ierr = nc_get_att(ncid, varid_time, units.data(), &refstring[0]);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  QString refString = QString::fromStdString(refstring.substr(14, 19));
  this->_refTime =
      QDateTime::fromString(refString, QStringLiteral("yyyy-MM-dd hh:mm:ss"));
  this->_refTime.setTimeSpec(Qt::UTC);

  std::vector<double> times(nsteps);
  this->_nSteps = nsteps;
  ierr = nc_get_var_double(ncid, varid_time, times.data());
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  timeList.resize(nsteps);
  for (size_t i = 0; i < this->_nSteps; i++) {
    timeList[i] =
        this->_refTime.addSecs(qRound64(times[i])).toMSecsSinceEpoch();
  }

  return MetOceanViewer::Error::NOERR;
}

int Dflow::_getVar(QString variable, int layer,
                   QVector<QVector<double>> &data) {
  if (this->_nDims[variable] == 2)
    return this->_getVar2D(variable, data);
  else if (this->_nDims[variable] == 3)
    return this->_getVar3D(variable, layer, data);
  else
    return MetOceanViewer::Error::DFLOW_ILLEGALDIMENSION;
}

int Dflow::_getVar2D(QString variable, QVector<QVector<double>> &data) {
  data.resize(this->_nStations);
  for (size_t i = 0; i < this->_nStations; i++) data[i].resize(this->_nSteps);

  int ncid;
  int varid = this->_varnames[variable];
  int ierr = nc_open(this->_filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  std::vector<size_t> start(2, 0);
  std::vector<size_t> count(2, 0);
  std::vector<double> d(this->_nSteps * this->_nStations);
  start[0] = 0;
  start[1] = 0;
  count[0] = this->_nSteps;
  count[1] = this->_nStations;
  ierr = nc_get_vara_double(ncid, varid, start.data(), count.data(), d.data());
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  for (size_t i = 0; i < this->_nSteps; i++)
    for (size_t j = 0; j < this->_nStations; j++) {
      if (d[i * this->_nStations + j] == -999.0)
        data[j][i] = HmdfStation::nullDataValue();
      else
        data[j][i] = d[i * this->_nStations + j];
    }

  ierr = nc_close(ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  return MetOceanViewer::Error::NOERR;
}

int Dflow::_getVar3D(QString variable, int layer,
                     QVector<QVector<double>> &data) {
  std::vector<double> d(this->_nSteps * this->_nStations, 0);
  std::vector<size_t> start(3, 0);
  std::vector<size_t> count(3, 0);

  data.resize(this->_nStations);
  for (size_t i = 0; i < this->_nStations; i++) data[i].resize(this->_nSteps);

  int ncid;
  int varid = this->_varnames[variable];
  int ierr = nc_open(this->_filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  start[0] = 0;
  start[1] = 0;
  start[2] = layer - 1;
  count[0] = this->_nSteps;
  count[1] = this->_nStations;
  count[2] = 1;
  ierr = nc_get_vara_double(ncid, varid, start.data(), count.data(), d.data());
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  for (size_t i = 0; i < this->_nSteps; i++) {
    for (size_t j = 0; j < this->_nStations; j++) {
      if (d[i * this->_nStations + j] == -999.0)
        data[j][i] = HmdfStation::nullDataValue();
      else
        data[j][i] = d[i * this->_nStations + j];
    }
  }

  ierr = nc_close(ncid);
  if (ierr != NC_NOERR) {
    this->error->setErrorCode(MetOceanViewer::Error::NETCDF);
    this->error->setNcErrorCode(ierr);
    return MetOceanViewer::Error::NETCDF;
  }

  return MetOceanViewer::Error::NOERR;
}
