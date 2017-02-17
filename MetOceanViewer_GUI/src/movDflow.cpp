#include "movDflow.h"
#include "netcdf"
#include "movImeds.h"
#include "movErrors.h"
#include <QtMath>

using namespace netCDF;
using namespace netCDF::exceptions;

MovDflow::MovDflow(QString filename, QObject *parent) : QObject(parent)
{
    this->_isInitialized = false;
    this->_readError = true;
    this->_filename = filename;
    this->_is3d = false;
    this->error = new movErrors(this);
    this->_nSteps = 0;
    this->_nStations = 0;
    this->_nLayers = 0;

    int ierr = this->_init();

    this->error->setErrorCode(ierr);

    if(this->error->isError())
    {
        this->_isInitialized = true;
        this->_readError = false;
    }
    else
    {
        this->_isInitialized = false;
        this->_readError = true;
    }
    return;
}


bool MovDflow::is3d()
{
    return this->_is3d;
}


QStringList MovDflow::getVaribleList()
{
    return QStringList(this->_plotvarnames);
}


int MovDflow::getNumLayers()
{
    return this->_nLayers;
}


bool MovDflow::variableIs3d(QString variable)
{
    if(variable=="velocity_magnitude")
    {
        if(this->_nDims["x_velocity"]==3)
            return true;
        else
            return false;
    }
    if(this->_nDims[variable]==3)
        return true;
    else
        return false;
}


int MovDflow::_get2DVelocityMagnitude(int layer, QVector<QVector<double> > &data)
{
    int ierr,i,j;
    QVector<QVector<double> > x_data,y_data;

    ierr = this->_getVar(QStringLiteral("x_velocity"),layer,x_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOXVELOCITY);
        return this->error->errorCode();
    }

    ierr = this->_getVar(QStringLiteral("y_velocity"),layer,y_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOYVELOCITY);
        return this->error->errorCode();
    }

    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
    {
        data[i].resize(this->_nSteps);
        for(j=0;j<this->_nSteps;j++)
        {
            data[i][j] = qSqrt(qPow(x_data[i][j],2.0)+qPow(y_data[i][j],2.0));
        }
    }
    return ERR_NOERR;
}


int MovDflow::_get2DVelocityDirection(int layer, QVector<QVector<double> > &data)
{
    int ierr,i,j;
    QVector<QVector<double> > x_data,y_data;

    ierr = this->_getVar(QStringLiteral("x_velocity"),layer,x_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOXVELOCITY);
        return this->error->errorCode();
    }

    ierr = this->_getVar(QStringLiteral("y_velocity"),layer,y_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOYVELOCITY);
        return this->error->errorCode();
    }

    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
    {
        data[i].resize(this->_nSteps);
        for(j=0;j<this->_nSteps;j++)
            data[i][j] = qAtan2(y_data[i][j],x_data[i][j])*180.0/M_PI;
    }
    return ERR_NOERR;
}


int MovDflow::_get3DVeloctiyMagnitude(int layer, QVector<QVector<double> > &data)
{
    int ierr,i,j;
    QVector<QVector<double> > x_data,y_data,z_data;

    ierr = this->_getVar(QStringLiteral("x_velocity"),layer,x_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOXVELOCITY);
        return this->error->errorCode();
    }

    ierr = this->_getVar(QStringLiteral("y_velocity"),layer,y_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOYVELOCITY);
        return this->error->errorCode();
    }

    ierr = this->_getVar(QStringLiteral("z_velocity"),layer,z_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOZVELOCITY);
        return this->error->errorCode();
    }

    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
    {
        data[i].resize(this->_nSteps);
        for(j=0;j<this->_nSteps;j++)
        {
            data[i][j] = qSqrt(qPow(x_data[i][j],2.0)+qPow(y_data[i][j],2.0)+qPow(z_data[i][j],2.0));
        }
    }
    return ERR_NOERR;
}


int MovDflow::_getWindVelocityMagnitude(QVector<QVector<double> > &data)
{
    int ierr,i,j;
     QVector<QVector<double> > x_data,y_data;

    ierr = this->_getVar(QStringLiteral("windx"),0,x_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOXVELOCITY);
        return this->error->errorCode();
    }
    ierr = this->_getVar(QStringLiteral("windy"),0,y_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOYVELOCITY);
        return this->error->errorCode();
    }
    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
    {
        data[i].resize(this->_nSteps);
        for(j=0;j<this->_nSteps;j++)
        {
            data[i][j] = qSqrt(qPow(x_data[i][j],2.0)+qPow(y_data[i][j],2.0));
        }
    }
    return ERR_NOERR;
}


int MovDflow::_getWindDirection(QVector<QVector<double> > &data)
{
    int ierr,i,j;
     QVector<QVector<double> > x_data,y_data;

    ierr = this->_getVar(QStringLiteral("windx"),0,x_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOXVELOCITY);
        return this->error->errorCode();
    }
    ierr = this->_getVar(QStringLiteral("windy"),0,y_data);
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_NOYVELOCITY);
        return this->error->errorCode();
    }
    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
    {
        data[i].resize(this->_nSteps);
        for(j=0;j<this->_nSteps;j++)
            data[i][j] = qAtan2(y_data[i][j],x_data[i][j])*180.0/M_PI;
    }
    return ERR_NOERR;
}



int MovDflow::getVariable(QString variable, int layer, MovImeds *imeds)
{
    int i,ierr;
    QVector<QDateTime> time;
    QVector<QVector<double> > data;

    ierr = this->_getTime(time);
    this->error->setErrorCode(ierr);
    if(this->error->isError())
        return this->error->errorCode();

    //...Check for derrived data or just retrieve the
    //   requested variable
    if(variable==QStringLiteral("2D_current_speed"))
        ierr = this->_get2DVelocityMagnitude(layer,data);
    else if(variable==QStringLiteral("2D_current_direction"))
        ierr = this->_get2DVelocityDirection(layer,data);
    else if(variable==QStringLiteral("3D_current_speed"))
        ierr = this->_get3DVeloctiyMagnitude(layer,data);
    else if(variable==QStringLiteral("wind_speed"))
        ierr = this->_getWindVelocityMagnitude(data);
    else if(variable==QStringLiteral("wind_direction"))
        ierr = this->_getWindDirection(data);
    else
        ierr = this->_getVar(variable,layer,data);

    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ierr);
        return this->error->errorCode();
    }


    imeds->success = false;
    imeds->nstations = this->_nStations;
    imeds->datum = QStringLiteral("dflowfm_datum");
    imeds->station.resize(this->_nStations);
    imeds->header1 = QStringLiteral("DFlowFM");
    imeds->header2 = QStringLiteral("DFlowFM");
    imeds->header3 = QStringLiteral("DFlowFM");
    for(i=0;i<this->_nStations;i++)
    {        
        imeds->station[i] = new MovImedsStation(this);

        imeds->station[i]->date = time;
        imeds->station[i]->data = data[i];

        imeds->station[i]->NumSnaps = this->_nSteps;
        imeds->station[i]->latitude = this->_yCoordinates[i];
        imeds->station[i]->longitude = this->_xCoordinates[i];
        imeds->station[i]->StationIndex = i;
        imeds->station[i]->StationID = i;
        imeds->station[i]->StationName = this->_stationNames[i];
    }
    imeds->success = true;

    return ERR_NOERR;
}


int MovDflow::_init()
{
    int ierr;

    ierr = this->_getPlottingVariables();
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_GETPLOTVARS);
        return this->error->errorCode();
    }

    ierr = this->_getStations();
    if(ierr!=ERR_NOERR)
    {
        this->error->setErrorCode(ERR_DFLOW_GETSTATIONS);
        return this->error->errorCode();
    }

    return ERR_NOERR;
}


int MovDflow::_get3d()
{

    int ierr,ncid;
    size_t nLayers;

    if(this->_dimnames.contains("laydimw"))
        this->_is3d = true;
    else
    {
        this->_is3d = false;
        return ERR_NOERR;
    }

    ierr = nc_open(this->_filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    ierr = nc_inq_dimlen(ncid,this->_dimnames["laydim"],&nLayers);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    this->_nLayers = (int)nLayers;

    return ERR_NOERR;
}


int MovDflow::_getPlottingVariables()
{
    int ncid,nvar,ndim;
    int nd;
    int i,ierr;
    QString sname;

    ierr = nc_open(this->_filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    this->error->setNcErrorCode(ierr);
    if(this->error->isNcError())
    {
        this->error->setErrorCode(ERR_NETCDF);
        return this->error->errorCode();
    }

    ierr = nc_inq_nvars(ncid,&nvar);
    this->error->setNcErrorCode(ierr);
    if(this->error->isNcError())
    {
        this->error->setErrorCode(ERR_NETCDF);
        ierr = nc_close(ncid);
        return this->error->errorCode();
    }

    ierr = nc_inq_ndims(ncid,&ndim);
    this->error->setNcErrorCode(ierr);
    if(this->error->isNcError())
    {
        this->error->setErrorCode(ERR_NETCDF);
        ierr = nc_close(ncid);
        return this->error->errorCode();
    }

    char * varname = (char*)malloc(sizeof(char)*(NC_MAX_NAME+1));
    int  * dims    = (int*)malloc(sizeof(int)*NC_MAX_DIMS);

    for(i=0;i<ndim;i++)
    {
        ierr = nc_inq_dimname(ncid,i,varname);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            this->error->setErrorCode(ERR_NETCDF);
            this->error->setNcErrorCode(ierr);
            ierr = nc_close(ncid);
            return ERR_NETCDF;
        }
        sname = QString(varname);
        this->_dimnames[sname] = i;
    }

    for(i=0;i<nvar;i++)
    {
        ierr = nc_inq_varname(ncid,i,varname);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            this->error->setErrorCode(ERR_NETCDF);
            this->error->setNcErrorCode(ierr);
            ierr = nc_close(ncid);
            return ERR_NETCDF;
        }
        sname = QString(varname);

        ierr = nc_inq_varndims(ncid,i,&nd);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            this->error->setErrorCode(ERR_NETCDF);
            this->error->setNcErrorCode(ierr);
            ierr = nc_close(ncid);
            return ERR_NETCDF;
        }

        this->_nDims[sname] = (int)nd;

        ierr = nc_inq_vardimid(ncid,i,dims);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            this->error->setErrorCode(ERR_NETCDF);
            this->error->setNcErrorCode(ierr);
            ierr = nc_close(ncid);
            return ERR_NETCDF;
        }

        if(nd==2)
        {
            if(dims[0]==this->_dimnames["time"] &&
               dims[1]==this->_dimnames["stations"])
                 this->_plotvarnames.append(sname);
        }
        else if(nd==3)
        {
            if(dims[0]==this->_dimnames["time"] &&
               dims[1]==this->_dimnames["stations"] &&
               dims[2]==this->_dimnames["laydim"])
                this->_plotvarnames.append(sname);
//            else if(dims[0]==this->_dimnames["time"] &&
//                    dims[1]==this->_dimnames["stations"] &&
//                    dims[2]==this->_dimnames["laydimw"])
//                this->_plotvarnames.append(sname);
        }
        this->_varnames[sname] = i;
    }

    free(varname);
    free(dims);

    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    ierr = this->_get3d();
    if(ierr!=ERR_NOERR)
        return ERR_DFLOW_3DVARS;

    if(this->is3d())
    {
        if(this->_plotvarnames.contains("x_velocity") &&
                this->_plotvarnames.contains("y_velocity") &&
                this->_plotvarnames.contains("z_velocity"))
            this->_plotvarnames.append("3D_current_speed");
    }

    if(this->_plotvarnames.contains("x_velocity") &&
            this->_plotvarnames.contains("y_velocity"))
    {
        this->_plotvarnames.append("2D_current_speed");
        this->_plotvarnames.append("2D_current_direction");
    }

    if(this->_plotvarnames.contains("windx") &&
            this->_plotvarnames.contains("windy"))
    {
        this->_plotvarnames.append("wind_speed");
        this->_plotvarnames.append("wind_direction");
    }

    return ERR_NOERR;

}


int MovDflow::_getStations()
{
    int i;
    size_t nstation,name_len;
    std::vector<size_t> start,count;

    NcFile file;
    file.open(this->_filename.toStdString(),NcFile::read);
    NcDim stationDimension = file.getDim("stations");
    NcDim stationNameLength = file.getDim("name_len");
    NcVar xvar = file.getVar("station_x_coordinate");
    NcVar yvar = file.getVar("station_y_coordinate");
    NcVar nameVar = file.getVar("station_name");

    nstation = stationDimension.getSize();
    this->_nStations = (int)nstation;
    name_len = stationNameLength.getSize();
    char *stationName = (char*)malloc(sizeof(char)*name_len);
    double *xcoor = (double*)malloc(sizeof(double)*nstation);
    double *ycoor = (double*)malloc(sizeof(double)*nstation);
    this->_xCoordinates.resize(this->_nStations);
    this->_yCoordinates.resize(this->_nStations);
    this->_stationNames.resize(this->_nStations);

    start.resize(2);
    count.resize(2);
    count[0] = 1;
    count[1] = name_len;

    for(i=0;i<nstation;i++)
    {
        start[0] = i;
        start[1] = 0;
        nameVar.getVar(start,count,stationName);
        this->_stationNames[i] = QString(stationName);
    }

    start.resize(1);
    count.resize(1);
    start[0] = 0;
    count[0] = nstation;

    xvar.getVar(start,count,xcoor);
    yvar.getVar(start,count,ycoor);

    for(i=0;i<this->_nStations;i++)
    {
        this->_xCoordinates[i] = xcoor[i];
        this->_yCoordinates[i] = ycoor[i];
    }

    file.close();

    free(xcoor);
    free(ycoor);
    free(stationName);

    return 0;
}


int MovDflow::_getTime(QVector<QDateTime> &timeList)
{
    int i,ierr,ncid;
    size_t nsteps,unitsLen;
    double * time;
    char * refstring;
    int varid_time = this->_varnames["time"];
    int dimid_time = this->_dimnames["time"];
    char * units = strdup("units");
    QString refString;

    ierr = nc_open(this->_filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        free(units);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    ierr = nc_inq_dimlen(ncid,dimid_time,&nsteps);
    if(ierr!=NC_NOERR)
    {
        free(units);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    ierr = nc_inq_attlen(ncid,varid_time,units,&unitsLen);
    if(ierr!=NC_NOERR)
    {
        free(units);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    refstring = (char*)malloc(sizeof(char)*unitsLen);

    ierr = nc_get_att(ncid,varid_time,units,refstring);
    if(ierr!=NC_NOERR)
    {
        free(units);
        free(refstring);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    refString = QString(refstring);
    refString = refString.mid(0,(int)unitsLen).right(19);
    free(units);
    free(refstring);

    this->_refTime = QDateTime::fromString(refString,QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    this->_refTime.setTimeSpec(Qt::UTC);

    timeList.resize((int)nsteps);
    time = (double*)malloc(sizeof(double)*nsteps);
    this->_nSteps = (int)nsteps;

    ierr = nc_get_var_double(ncid,varid_time,time);
    if(ierr!=NC_NOERR)
    {
        free(time);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    for(i=0;i<this->_nSteps;i++)
        timeList[i] = this->_refTime.addMSecs(qRound64(time[i]*1000.0));

    free(time);

    return ERR_NOERR;
}


int MovDflow::_getVar(QString variable, int layer, QVector<QVector<double> > &data)
{
    if(this->_nDims[variable]==2)
        return this->_getVar2D(variable,data);
    else if(this->_nDims[variable]==3)
        return this->_getVar3D(variable,layer,data);
    else
        return ERR_DFLOW_ILLEGALDIMENSION;
}


int MovDflow::_getVar2D(QString variable, QVector<QVector<double> > &data)
{
    int i,j,ierr,ncid,varid;
    double *d = (double*)malloc(sizeof(double)*this->_nSteps*this->_nStations);
    size_t *start  = (size_t*)malloc(sizeof(size_t)*2);
    size_t *count = (size_t*)malloc(sizeof(size_t)*2);

    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
        data[i].resize(this->_nSteps);

    varid = this->_varnames[variable];
    ierr = nc_open(this->_filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        free(start);
        free(count);
        free(d);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    start[0] = 0;
    start[1] = 0;
    count[0] = this->_nSteps;
    count[1] = this->_nStations;
    ierr = nc_get_vara_double(ncid,varid,start,count,d);
    if(ierr!=NC_NOERR)
    {
        free(d);
        free(start);
        free(count);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    for(i=0;i<this->_nSteps;i++)
        for(j=0;j<this->_nStations;j++)
            data[j][i] = d[i*this->_nStations+j];

    free(d);
    free(start);
    free(count);

    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    return ERR_NOERR;

}


int MovDflow::_getVar3D(QString variable, int layer, QVector<QVector<double> > &data)
{
    int i,j,ierr,ncid,varid;
    double *d = (double*)malloc(sizeof(double)*this->_nSteps*this->_nStations);
    size_t *start  = (size_t*)malloc(sizeof(size_t)*3);
    size_t *count = (size_t*)malloc(sizeof(size_t)*3);

    data.resize(this->_nStations);
    for(i=0;i<this->_nStations;i++)
        data[i].resize(this->_nSteps);

    varid = this->_varnames[variable];
    ierr = nc_open(this->_filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        free(d);
        free(start);
        free(count);
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    start[0] = 0;
    start[1] = 0;
    start[2] = layer-1;
    count[0] = this->_nSteps;
    count[1] = this->_nStations;
    count[2] = 1;
    ierr = nc_get_vara_double(ncid,varid,start,count,d);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    for(i=0;i<this->_nSteps;i++)
        for(j=0;j<this->_nStations;j++)
            data[j][i] = d[i*this->_nStations+j];

    free(d);
    free(start);
    free(count);

    ierr = nc_close(ncid);
    if(ierr!=NC_NOERR)
    {
        this->error->setErrorCode(ERR_NETCDF);
        this->error->setNcErrorCode(ierr);
        return ERR_NETCDF;
    }

    return ERR_NOERR;

}
