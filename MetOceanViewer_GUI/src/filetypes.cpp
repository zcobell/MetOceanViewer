#include "filetypes.h"
#include "netcdf.h"
#include <QMap>
#include <QFileInfo>

QMap<QString,int> filetypeMapString = {
    {QStringLiteral("NETCDF-ADCIRC"),FILETYPE_NETCDF_ADCIRC},
    {QStringLiteral("NETCDF-DFLOW"),FILETYPE_NETCDF_DFLOW},
    {QStringLiteral("ASCII-ADCIRC"),FILETYPE_ASCII_ADCIRC},
    {QStringLiteral("ASCII-IMEDS"),FILETYPE_ASCII_IMEDS}
};

QMap<int,QString> filetypeMapInt = {
    {FILETYPE_NETCDF_ADCIRC,QStringLiteral("NETCDF-ADCIRC")},
    {FILETYPE_NETCDF_DFLOW,QStringLiteral("NETCDF-DFLOW")},
    {FILETYPE_ASCII_ADCIRC,QStringLiteral("ASCII-ADCIRC")},
    {FILETYPE_ASCII_IMEDS,QStringLiteral("ASCII-IMEDS")}
};


filetypes::filetypes(QObject *parent) : QObject(parent)
{

}


int filetypes::getIntegerFiletype(QString filename)
{
    if(filetypes::_checkNetcdfAdcirc(filename))
        return FILETYPE_NETCDF_ADCIRC;
    if(filetypes::_checkNetcdfDflow(filename))
        return FILETYPE_NETCDF_DFLOW;
    if(filetypes::_checkASCIIImeds(filename))
        return FILETYPE_ASCII_IMEDS;
    if(filetypes::_checkASCIIAdcirc(filename))
        return FILETYPE_ASCII_ADCIRC;
    return FILETYPE_ERROR;
}

QString filetypes::getStringFiletype(QString filename)
{
    if(filetypes::_checkNetcdfAdcirc(filename))
        return QStringLiteral("NETCDF-ADCIRC");
    if(filetypes::_checkNetcdfDflow(filename))
        return QStringLiteral("NETCDf-DFLOW");
    if(filetypes::_checkASCIIAdcirc(filename))
        return QStringLiteral("ASCII-ADCIRC");
    if(filetypes::_checkASCIIImeds(filename))
        return QStringLiteral("ASCII-IMEDS");
    return QStringLiteral("ERROR");
}


bool filetypes::_checkNetcdfAdcirc(QString filename)
{
    int ncid,ierr;
    size_t attlen;
    char * attname = strdup("model");
    char * model;
    QString models;

    ierr = nc_open(filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=0)
        return false;

    ierr = nc_inq_attlen(ncid,NC_GLOBAL,attname,&attlen);
    if(ierr!=0)
    {
        ierr = nc_close(ncid);
        return false;
    }

    model = (char*)malloc(sizeof(char)*attlen);
    ierr = nc_get_att(ncid,NC_GLOBAL,attname,model);
    if(ierr!=0)
    {
        ierr = nc_close(ncid);
        return false;
    }
    ierr = nc_close(ncid);

    models = QString(model).mid(0,attlen);

    if(models==QStringLiteral("ADCIRC"))
        return true;
    else
        return false;
}


bool filetypes::_checkNetcdfDflow(QString filename)
{
    int    ierr,ncid;
    int    varid_stationx,varid_stationy;
    char * varname_stationx = strdup("station_x_coordinate");
    char * varname_stationy = strdup("station_y_coordinate");

    ierr = nc_open(filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=0)
        return false;

    ierr = nc_inq_varid(ncid,varname_stationx,&varid_stationx);
    if(ierr!=0)
    {
        ierr = nc_close(ncid);
        return false;
    }

    ierr = nc_inq_varid(ncid,varname_stationy,&varid_stationy);
    if(ierr!=0)
    {
        ierr = nc_close(ncid);
        return false;
    }

    ierr = nc_close(ncid);

    return true;
}


bool filetypes::_checkASCIIAdcirc(QString filename)
{
    QFileInfo file(filename);
    QString suffix = file.suffix();
    if(suffix=="61" || suffix=="62" ||
       suffix=="71" || suffix=="72")
        return true;
    return false;
}


bool filetypes::_checkASCIIImeds(QString filename)
{
    QFileInfo file(filename);
    QString suffix = file.suffix().toUpper();
    if(suffix=="IMEDS")
        return true;
    return false;
}

QString filetypes::integerFiletypeToString(int filetype)
{
    return filetypeMapInt[filetype];
}
