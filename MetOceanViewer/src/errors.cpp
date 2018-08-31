#include "errors.h"
#include "netcdf.h"

static QMap<int, QString> _errorTable = {
    {MetOceanViewer::Error::NOERR, QObject::QObject::tr("No error detected.")},
    {MetOceanViewer::Error::NOAA_INVALIDDATERANGE, QObject::tr("Invalid date range.")},
    {MetOceanViewer::Error::USGS_ARCHIVEONLY, QObject::tr("Data only available from archive.")},
    {MetOceanViewer::Error::USGS_SERVERREADERROR,
     QObject::tr("Error retrieving data form the USGS server.")},
    {MetOceanViewer::Error::USGS_READDATA,
     QObject::tr("Error reading data provided by USGS server.")},
    {MetOceanViewer::Error::CANNOT_OPEN_FILE,
     QObject::tr("Error while attempting to open the file.")},
    {MetOceanViewer::Error::WRONG_NUMBER_OF_STATIONS,
     QObject::tr("Invalid number of stations detected.")},
    {MetOceanViewer::Error::NETCDF, QObject::tr("Generic netCDF error.")},
    {MetOceanViewer::Error::NO_VARIABLE_FOUND, QObject::tr("Vairable not found in the dataset.")},
    {MetOceanViewer::Error::DFLOW_GETPLOTVARS,
     QObject::tr("Error retrieving the plot variables from the DFlow file.")},
    {MetOceanViewer::Error::DFLOW_GETSTATIONS,
     QObject::tr(
         "Error retrieving the station locations from the DFlow file.")},
    {MetOceanViewer::Error::DFLOW_NOXVELOCITY,
     QObject::tr("Error finding the x-velocity variable in the DFlow file.")},
    {MetOceanViewer::Error::DFLOW_NOYVELOCITY,
     QObject::tr("Error finding the y-velocity variable in the DFlow file.")},
    {MetOceanViewer::Error::DFLOW_NOZVELOCITY,
     QObject::tr("Error finding the z-velocity variable in the DFlow file.")},
    {MetOceanViewer::Error::DFLOW_3DVARS, QObject::tr("Error determining 3D file parameters.")},
    {MetOceanViewer::Error::DFLOW_VARNOTFOUND,
     QObject::tr("Error locating variablein DFlow file.")},
    {MetOceanViewer::Error::DFLOW_ILLEGALDIMENSION,
     QObject::tr("Illegal dimension size in DFlow file.")},
    {MetOceanViewer::Error::DFLOW_FILEREADERROR,
     QObject::tr("Generic error while reading DFlow file.")},
    {MetOceanViewer::Error::ADCIRC_ASCIIREADERROR,
     QObject::tr("Generic error while reading ADCIRC ASCII file.")},
    {MetOceanViewer::Error::ADCIRC_NETCDFREADERROR,
     QObject::tr("Generic error while reading ADCIRC netCDF file.")},
    {MetOceanViewer::Error::IMEDS_FILEREADERROR,
     QObject::tr("Generic error while reading IMEDS format file.")},
    {MetOceanViewer::Error::INVALIDFILEFORMAT, QObject::tr("Unknown file type specified")},
    {MetOceanViewer::Error::GENERICFILEREADERROR, QObject::tr("Generic file read error.")},
    {MetOceanViewer::Error::ADCIRC_ASCIITOIMEDS,
     QObject::tr(
         "Error converting ADCIRC ASCII format to internal IMEDS structure")},
    {MetOceanViewer::Error::ADCIRC_NETCDFREADERROR,
     QObject::tr(
         "Error converting ASCIRC netCDF format to internal IMEDS structure")},
    {MetOceanViewer::Error::BUILDSTATIONLIST, QObject::tr("Error building station list.")},
    {MetOceanViewer::Error::BUILDREVISEDIMEDS,
     QObject::tr("Error building final internal IMEDS structure")},
    {MetOceanViewer::Error::PROJECTSTATIONS, QObject::tr("Error from Proj4 station projections.")},
    {MetOceanViewer::Error::MARKERSELECTION, QObject::tr("Error selecting markers on map.")}};

Errors::Errors(QObject *parent) : QObject(parent) {
  this->_ncerr = NC_NOERR;
  this->_error = MetOceanViewer::Error::NOERR;
}

QString Errors::toString() {
  if (this->_error == MetOceanViewer::Error::NETCDF) {
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

bool Errors::isError() {
  if (this->_error != MetOceanViewer::Error::NOERR)
    return true;
  else
    return false;
}

bool Errors::isNcError() {
  if (this->_ncerr != NC_NOERR)
    return true;
  else
    return false;
}

int Errors::errorCode() { return this->_error; }

int Errors::ncError() { return this->_ncerr; }

void Errors::setErrorCode(int ierr) {
  this->_error = ierr;
  return;
}

void Errors::setNcErrorCode(int ncerr) {
  this->_ncerr = ncerr;
  return;
}
