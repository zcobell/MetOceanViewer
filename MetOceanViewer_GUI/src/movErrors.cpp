#include "moverrors.h"
#include "netcdf.h"

static QMap<int,QString> _errorTable = {
    {ERR_NOERR,QStringLiteral("No error detected.")},
    {ERR_NOAA_INVALIDDATERANGE,QStringLiteral("Invalid date range.")},
    {ERR_USGS_ARCHIVEONLY,QStringLiteral("Data only available from archive.")},
    {ERR_USGS_SERVERREADERROR,QStringLiteral("Error retrieving data form the USGS server.")},
    {ERR_USGS_READDATA,QStringLiteral("Error reading data provided by USGS server.")},
    {ERR_CANNOT_OPEN_FILE,QStringLiteral("Error while attempting to open the file.")},
    {ERR_WRONG_NUMBER_OF_STATIONS,QStringLiteral("Invalid number of stations detected.")},
    {ERR_NETCDF,QStringLiteral("Generic netCDF error.")},
    {ERR_NO_VARIABLE_FOUND,QStringLiteral("Vairable not found in the dataset.")},
    {ERR_DFLOW_GETPLOTVARS,QStringLiteral("Error retrieving the plot variables from the DFlow file.")},
    {ERR_DFLOW_GETSTATIONS,QStringLiteral("Error retrieving the station locations from the DFlow file.")},
    {ERR_DFLOW_NOXVELOCITY,QStringLiteral("Error finding the x-velocity variable in the DFlow file.")},
    {ERR_DFLOW_NOYVELOCITY,QStringLiteral("Error finding the y-velocity variable in the DFlow file.")},
    {ERR_DFLOW_NOZVELOCITY,QStringLiteral("Error finding the z-velocity variable in the DFlow file.")},
    {ERR_DFLOW_3DVARS,QStringLiteral("Error determining 3D file parameters.")},
    {ERR_DFLOW_VARNOTFOUND,QStringLiteral("Error locating variablein DFlow file.")},
    {ERR_DFLOW_ILLEGALDIMENSION,QStringLiteral("Illegal dimension size in DFlow file.")},
    {ERR_DFLOW_FILEREADERROR,QStringLiteral("Generic error while reading DFlow file.")},
    {ERR_ADCIRC_ASCIIREADERROR,QStringLiteral("Generic error while reading ADCIRC ASCII file.")},
    {ERR_ADCIRC_NETCDFREADERROR,QStringLiteral("Generic error while reading ADCIRC netCDF file.")},
    {ERR_IMEDS_FILEREADERROR,QStringLiteral("Generic error while reading IMEDS format file.")},
    {ERR_INVALIDFILEFORMAT,QStringLiteral("Unknown file type specified")},
    {ERR_GENERICFILEREADERROR,QStringLiteral("Generic file read error.")},
    {ERR_ADCIRC_ASCIITOIMEDS,QStringLiteral("Error converting ADCIRC ASCII format to internal IMEDS structure")},
    {ERR_ADCIRC_NETCDFREADERROR,QStringLiteral("Error converting ASCIRC netCDF format to internal IMEDS structure")},
    {ERR_BUILDSTATIONLIST,QStringLiteral("Error building station list.")},
    {ERR_BUILDREVISEDIMEDS,QStringLiteral("Error building final internal IMEDS structure")},
    {ERR_PROJECTSTATIONS,QStringLiteral("Error from Proj4 station projections.")},
    {ERR_MARKERSELECTION,QStringLiteral("Error selecting markers on map.")}
};


movErrors::movErrors(QObject *parent) : QObject(parent)
{
    this->_ncerr = NC_NOERR;
    this->_error = ERR_NOERR;
}


QString movErrors::toString()
{
    if(this->_error==ERR_NETCDF)
    {
        QString ncerr = QString(nc_strerror(this->_ncerr));
        return _errorTable[this->_error]+QStringLiteral(" Code: ")+
                QString::number(this->_error)+QStringLiteral(" netCDF error string: ")+
                ncerr;
    }
    else
        if(_errorTable.contains(this->_error))
            return _errorTable[this->_error]+QStringLiteral(" Code: ")+
                    QString::number(this->_error);
        else
            return QStringLiteral("Generic error encountered. Code: ")+
                    QString::number(this->_error);
}


bool movErrors::isError()
{
    if(this->_error!=ERR_NOERR)
        return true;
    else
        return false;
}


bool movErrors::isNcError()
{
    if(this->_ncerr!=NC_NOERR)
        return true;
    else
        return false;
}


int movErrors::errorCode()
{
    return this->_error;
}


int movErrors::ncError()
{
    return this->_ncerr;
}


void movErrors::setErrorCode(int ierr)
{
    this->_error = ierr;
    return;
}


void movErrors::setNcErrorCode(int ncerr)
{
    this->_ncerr = ncerr;
    return;
}
