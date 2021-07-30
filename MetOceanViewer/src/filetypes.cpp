#include "filetypes.h"
#include <QFileInfo>
#include <QMap>
#include "netcdf.h"

static QMap<QString, int> filetypeMapString = {
    {QStringLiteral("NETCDF-ADCIRC"), MetOceanViewer::FileType::NETCDF_ADCIRC},
    {QStringLiteral("NETCDF-DFLOW"), MetOceanViewer::FileType::NETCDF_DFLOW},
    {QStringLiteral("ASCII-ADCIRC"), MetOceanViewer::FileType::ASCII_ADCIRC},
    {QStringLiteral("ASCII-IMEDS"), MetOceanViewer::FileType::ASCII_IMEDS},
    {QStringLiteral("NETCDF-GENERIC"),
     MetOceanViewer::FileType::NETCDF_GENERIC}};

static QMap<int, QString> filetypeMapInt = {
    {MetOceanViewer::FileType::NETCDF_ADCIRC, QStringLiteral("NETCDF-ADCIRC")},
    {MetOceanViewer::FileType::NETCDF_DFLOW, QStringLiteral("NETCDF-DFLOW")},
    {MetOceanViewer::FileType::ASCII_ADCIRC, QStringLiteral("ASCII-ADCIRC")},
    {MetOceanViewer::FileType::ASCII_IMEDS, QStringLiteral("ASCII-IMEDS")},
    {MetOceanViewer::FileType::NETCDF_GENERIC,
     QStringLiteral("NETCDF-GENERIC")}};

Filetypes::Filetypes(QObject *parent) : QObject(parent) {}

int Filetypes::getIntegerFiletype(QString filename) {
  if (Filetypes::_checkNetcdfGeneric(filename))
    return MetOceanViewer::FileType::NETCDF_GENERIC;
  if (Filetypes::_checkNetcdfAdcirc(filename))
    return MetOceanViewer::FileType::NETCDF_ADCIRC;
  if (Filetypes::_checkNetcdfDflow(filename))
    return MetOceanViewer::FileType::NETCDF_DFLOW;
  if (Filetypes::_checkASCIIImeds(filename))
    return MetOceanViewer::FileType::ASCII_IMEDS;
  if (Filetypes::_checkASCIIAdcirc(filename))
    return MetOceanViewer::FileType::ASCII_ADCIRC;
  return MetOceanViewer::FileType::FILETYPE_ERROR;
}

QString Filetypes::getStringFiletype(QString filename) {
  if (Filetypes::_checkNetcdfGeneric(filename))
    return QStringLiteral("NETCDF-GENERIC");
  if (Filetypes::_checkNetcdfAdcirc(filename))
    return QStringLiteral("NETCDF-ADCIRC");
  if (Filetypes::_checkNetcdfDflow(filename))
    return QStringLiteral("NETCDf-DFLOW");
  if (Filetypes::_checkASCIIAdcirc(filename))
    return QStringLiteral("ASCII-ADCIRC");
  if (Filetypes::_checkASCIIImeds(filename))
    return QStringLiteral("ASCII-IMEDS");
  return QStringLiteral("ERROR");
}

bool Filetypes::_checkNetcdfAdcirc(QString filename) {
  int ncid, ierr;
  size_t attlen;
  std::string attname = "model";

  ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != 0) {
    return false;
  }

  ierr = nc_inq_attlen(ncid, NC_GLOBAL, &attname[0], &attlen);
  if (ierr != 0) {
    ierr = nc_close(ncid);
    return false;
  }

  std::string model(attlen, ' ');
  ierr = nc_get_att(ncid, NC_GLOBAL, &attname[0], &model[0]);
  if (ierr != 0) {
    nc_close(ncid);
    return false;
  }
  nc_close(ncid);

  QString models = QString::fromStdString(model).mid(0, (int)attlen);

  if (models == QStringLiteral("ADCIRC"))
    return true;
  else
    return false;
}

bool Filetypes::_checkNetcdfDflow(QString filename) {

  std::string varname_stationx = "station_x_coordinate";
  std::string varname_stationy = "station_y_coordinate";

  int ncid;
  int ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != 0) {
    return false;
  }

  int varid_stationx;
  ierr = nc_inq_varid(ncid, &varname_stationx[0], &varid_stationx);
  if (ierr != 0) {
    ierr = nc_close(ncid);
    return false;
  }

  int varid_stationy;
  ierr = nc_inq_varid(ncid, &varname_stationy[0], &varid_stationy);
  if (ierr != 0) {
    ierr = nc_close(ncid);
    return false;
  }

  ierr = nc_close(ncid);

  return true;
}

bool Filetypes::_checkASCIIAdcirc(QString filename) {
  QFileInfo file(filename);
  QString suffix = file.suffix();
  if (suffix == "61" || suffix == "62" || suffix == "71" || suffix == "72")
    return true;
  return false;
}

bool Filetypes::_checkASCIIImeds(QString filename) {
  QFileInfo file(filename);
  QString suffix = file.suffix().toUpper();
  if (suffix == "IMEDS") return true;
  return false;
}

bool Filetypes::_checkNetcdfGeneric(QString filename) {
  int ierr, ncid, varid;
  ierr = nc_open(filename.toStdString().c_str(), NC_NOWRITE, &ncid);
  if (ierr != 0) return false;
  ierr = nc_inq_varid(ncid, "time_station_0001", &varid);
  nc_close(ncid);
  if (ierr != 0) return false;
  return true;
}

QString Filetypes::integerFiletypeToString(int filetype) {
  return filetypeMapInt[filetype];
}
