#include "movDflow.h"
#include "netcdf"
#include "movImeds.h"
#include <QDebug>

using namespace netCDF;
using namespace netCDF::exceptions;

MovDflow::MovDflow(QString filename, QObject *parent) : QObject(parent)
{
    this->_isInitialized = false;
    this->_readError = true;
    this->_filename = filename;
    int ierr = this->_init();
    if(ierr==0)
    {
        this->_isInitialized = true;
        this->_readError = false;
    }
    return;
}


QStringList MovDflow::getVaribleList()
{
    return QStringList(this->_plotvarnames);
}


int MovDflow::getVariable(QString variable, MovImeds *imeds)
{
    int i,ierr;
    QVector<QDateTime> time;
    QVector<QVector<double> > data;

    ierr = this->_getTime(time);
    if(ierr!=0)
        return -1;
    ierr = this->_getVar(variable,data);
    if(ierr!=0)
        return -1;

    imeds->nstations = this->_nStations;
    imeds->datum = "dflowfm_datum";
    imeds->station.resize(this->_nStations);
    imeds->header1 = "DFlowFM";
    imeds->header2 = "DFlowFM";
    imeds->header3 = "DFlowFM";
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

    return 0;
}


int MovDflow::_init()
{
    int ierr;
    ierr = this->_getPlottingVariables();
    if(ierr!=0)
        return -1;
    ierr = this->_getStations();
    if(ierr!=0)
        return -1;
    return 0;
}


int MovDflow::_getPlottingVariables()
{
    int ncid,nvar,ndim;
    int nd;
    int i,ierr;
    QString sname;

    char * varname = (char*)malloc(sizeof(char)*(NC_MAX_NAME+1));
    int  * dims    = (int*)malloc(sizeof(int)*NC_MAX_DIMS);

    ierr = nc_open(this->_filename.toStdString().c_str(),NC_NOWRITE,&ncid);
    if(ierr!=NC_NOERR)
    {
        free(varname);
        free(dims);
        return -1;
    }

    ierr = nc_inq_nvars(ncid,&nvar);
    if(ierr!=NC_NOERR)
    {
        free(varname);
        free(dims);
        ierr = nc_close(ncid);
        return -1;
    }

    ierr = nc_inq_ndims(ncid,&ndim);
    if(ierr!=NC_NOERR)
    {
        free(varname);
        free(dims);
        ierr = nc_close(ncid);
        return -1;
    }

    for(i=0;i<ndim;i++)
    {
        ierr = nc_inq_dimname(ncid,i,varname);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            ierr = nc_close(ncid);
            return -1;
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
            ierr = nc_close(ncid);
            return -1;
        }
        sname = QString(varname);

        ierr = nc_inq_varndims(ncid,i,&nd);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            ierr = nc_close(ncid);
            return -1;
        }

        ierr = nc_inq_vardimid(ncid,i,dims);
        if(ierr!=NC_NOERR)
        {
            free(varname);
            free(dims);
            ierr = nc_close(ncid);
            return -1;
        }

        if(nd==2)
        {
            if(dims[0]==this->_dimnames["time"] &&
               dims[1]==this->_dimnames["stations"])
                 this->_plotvarnames.append(sname);
        }
        this->_varnames[sname] = i;
    }

    free(varname);
    free(dims);

    ierr = nc_close(ncid);

    return 0;

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
    this->_nStations = nstation;
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
        return -1;

    ierr = nc_inq_dimlen(ncid,dimid_time,&nsteps);
    if(ierr!=NC_NOERR)
        return -1;

    ierr = nc_inq_attlen(ncid,varid_time,units,&unitsLen);
    if(ierr!=NC_NOERR)
        return -1;

    refstring = (char*)malloc(sizeof(char)*unitsLen);

    ierr = nc_get_att(ncid,varid_time,units,refstring);
    if(ierr!=NC_NOERR)
    {
        free(refstring);
        return -1;
    }

    refString = QString(refstring);
    refString = refString.mid(0,unitsLen).right(19);
    free(refstring);

    this->_refTime = QDateTime::fromString(refString,"yyyy-MM-dd hh:mm:ss");
    this->_refTime.setTimeSpec(Qt::UTC);

    timeList.resize(nsteps);
    time = (double*)malloc(sizeof(double)*nsteps);
    this->_nSteps = nsteps;

    ierr = nc_get_var_double(ncid,varid_time,time);
    if(ierr!=NC_NOERR)
    {
        free(time);
        return -1;
    }

    for(i=0;i<this->_nSteps;i++)
        timeList[i] = this->_refTime.addMSecs(qRound64(time[i]*1000.0));

    return 0;
}


int MovDflow::_getVar(QString variable, QVector<QVector<double> > &data)
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

    start[0] = 0;
    start[1] = 0;
    count[0] = this->_nSteps;
    count[1] = this->_nStations;
    ierr = nc_get_vara_double(ncid,varid,start,count,d);

    for(i=0;i<this->_nSteps;i++)
        for(j=0;j<this->_nStations;j++)
            data[j][i] = d[i*this->_nStations+j];

    free(d);
    free(start);
    free(count);

    ierr = nc_close(ncid);

    return ierr;

}
