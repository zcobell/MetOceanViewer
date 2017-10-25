#include "movErrors.h"
#include "netcdf.h"

static QMap<int, QString> _errorTable = {
    {ERR_NOERR, QObject::QObject::tr("No error detected.")},
    {ERR_NOAA_INVALIDDATERANGE, QObject::tr("Invalid date range.")},
    {ERR_USGS_ARCHIVEONLY, QObject::tr("Data only available from archive.")},
    {ERR_USGS_SERVERREADERROR,
     QObject::tr("Error retrieving data form the USGS server.")},
    {ERR_USGS_READDATA,
     QObject::tr("Error reading data provided by USGS server.")},
    {ERR_CANNOT_OPEN_FILE,
     QObject::tr("Error while attempting to open the file.")},
    {ERR_WRONG_NUMBER_OF_STATIONS,
     QObject::tr("Invalid number of stations detected.")},
    {ERR_NETCDF, QObject::tr("Generic netCDF error.")},
    {ERR_NO_VARIABLE_FOUND, QObject::tr("Vairable not found in the dataset.")},
    {ERR_DFLOW_GETPLOTVARS,
     QObject::tr("Error retrieving the plot variables from the DFlow file.")},
    {ERR_DFLOW_GETSTATIONS,
     QObject::tr(
         "Error retrieving the station locations from the DFlow file.")},
    {ERR_DFLOW_NOXVELOCITY,
     QObject::tr("Error finding the x-velocity variable in the DFlow file.")},
    {ERR_DFLOW_NOYVELOCITY,
     QObject::tr("Error finding the y-velocity variable in the DFlow file.")},
    {ERR_DFLOW_NOZVELOCITY,
     QObject::tr("Error finding the z-velocity variable in the DFlow file.")},
    {ERR_DFLOW_3DVARS, QObject::tr("Error determining 3D file parameters.")},
    {ERR_DFLOW_VARNOTFOUND,
     QObject::tr("Error locating variablein DFlow file.")},
    {ERR_DFLOW_ILLEGALDIMENSION,
     QObject::tr("Illegal dimension size in DFlow file.")},
    {ERR_DFLOW_FILEREADERROR,
     QObject::tr("Generic error while reading DFlow file.")},
    {ERR_ADCIRC_ASCIIREADERROR,
     QObject::tr("Generic error while reading ADCIRC ASCII file.")},
    {ERR_ADCIRC_NETCDFREADERROR,
     QObject::tr("Generic error while reading ADCIRC netCDF file.")},
    {ERR_IMEDS_FILEREADERROR,
     QObject::tr("Generic error while reading IMEDS format file.")},
    {ERR_INVALIDFILEFORMAT, QObject::tr("Unknown file type specified")},
    {ERR_GENERICFILEREADERROR, QObject::tr("Generic file read error.")},
    {ERR_ADCIRC_ASCIITOIMEDS,
     QObject::tr(
         "Error converting ADCIRC ASCII format to internal IMEDS structure")},
    {ERR_ADCIRC_NETCDFREADERROR,
     QObject::tr(
         "Error converting ASCIRC netCDF format to internal IMEDS structure")},
    {ERR_BUILDSTATIONLIST, QObject::tr("Error building station list.")},
    {ERR_BUILDREVISEDIMEDS,
     QObject::tr("Error building final internal IMEDS structure")},
    {ERR_PROJECTSTATIONS, QObject::tr("Error from Proj4 station projections.")},
    {ERR_MARKERSELECTION, QObject::tr("Error selecting markers on map.")}};

movErrors::movErrors(QObject *parent) : QObject(parent) {
  this->_ncerr = NC_NOERR;
  this->_error = ERR_NOERR;
}

QString movErrors::toString() {
  if (this->_error == ERR_NETCDF) {
    QString ncerr = QString(nc_strerror(this->_ncerr));
    return _errorTable[this->_error] + tr(" Code: ") +
           QString::number(this->_error) + tr(" netCDF error string: ") + ncerr;
  } else if (_errorTable.contains(this->_error))
    return _errorTable[this->_error] + tr(" Code: ") +
           QString::number(this->_error);
  else
    return tr("Generic error encountered. Code: ") +
           QString::number(this->_error);
}

bool movErrors::isError() {
  if (this->_error != ERR_NOERR)
    return true;
  else
    return false;
}

bool movErrors::isNcError() {
  if (this->_ncerr != NC_NOERR)
    return true;
  else
    return false;
}

int movErrors::errorCode() { return this->_error; }

int movErrors::ncError() { return this->_ncerr; }

void movErrors::setErrorCode(int ierr) {
  this->_error = ierr;
  return;
}

void movErrors::setNcErrorCode(int ncerr) {
  this->_ncerr = ncerr;
  return;
}
