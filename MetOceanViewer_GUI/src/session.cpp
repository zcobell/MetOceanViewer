/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------*/
#include "session.h"
#include "filetypes.h"
#include "generic.h"

Session::Session(QTableWidget *inTableWidget,
                       QLineEdit *inPlotTitleWidget, QLineEdit *inXLabelWidget,
                       QLineEdit *inYLabelWidget, QDateEdit *inStartDateEdit,
                       QDateEdit *inEndDateEdit, QDoubleSpinBox *inYMinSpinBox,
                       QDoubleSpinBox *inYMaxSpinBox, QCheckBox *incheckAllData,
                       QCheckBox *inCheckYAuto, QString &inPreviousDirectory,
                       QObject *parent)
    : QObject(parent) {

  this->tableWidget = inTableWidget;
  this->plotTitleWidget = inPlotTitleWidget;
  this->xLabelWidget = inXLabelWidget;
  this->yLabelWidget = inYLabelWidget;
  this->startDateEdit = inStartDateEdit;
  this->endDateEdit = inEndDateEdit;
  this->yMinSpinBox = inYMinSpinBox;
  this->yMaxSpinBox = inYMaxSpinBox;
  this->checkAllData = incheckAllData;
  this->checkYAuto = inCheckYAuto;
  this->previousDirectory = inPreviousDirectory;
  this->sessionFileName = QString();
}

int Session::setSessionFilename(QString filename) {
  this->sessionFileName = filename;
  return 0;
}

QString Session::getSessionFilename() { return this->sessionFileName; }

int Session::save() {
  int ierr, ncid, i;
  int dimid_ntimeseries, dimid_one;
  int varid_filename, varid_colors, varid_units, varid_names;
  int varid_xshift, varid_yshift, varid_type, varid_coldstart;
  int varid_stationfile, varid_plottitle, varid_xlabel, varid_ylabel;
  int varid_startdate, varid_enddate, varid_precision, varid_ymin, varid_ymax;
  int varid_autodate, varid_autoy, varid_checkState, varid_epsg,
      varid_dflowlayer;
  int varid_dflowvar;
  int dims_1d[1];
  int nTimeseries;
  QString relPath, TempFile, Directory, tempString;
  QByteArray tempByte;
  size_t start[1];
  size_t iu;
  double mydatadouble[1];
  int mydataint[1];
  const char *mydatastring[1];

  QFile Session(this->sessionFileName);

  QVector<QString> filenames_ts;
  QVector<int> filetype_ts;
  QVector<QString> colors_ts;
  QVector<double> units_ts;
  QVector<QString> seriesname_ts;
  QVector<double> xshift_ts;
  QVector<double> yshift_ts;
  QVector<QString> date_ts;
  QVector<QString> stationfile_ts;
  QVector<int> checkStates_ts;
  QVector<int> epsg_ts;
  QVector<QString> dflowvar_ts;
  QVector<int> dflowlayer_ts;

  // Remove the old file
  if (Session.exists())
    Session.remove();

  // Get the path of the session file so we can save a relative path later
  Generic::splitPath(this->sessionFileName, TempFile, Directory);
  QDir CurrentDir(Directory);

  ierr = Generic::NETCDF_ERR(
      nc_create(this->sessionFileName.toUtf8(), NC_NETCDF4, &ncid));
  if (ierr != NC_NOERR)
    return 1;

  // Start setting up the definitions
  nTimeseries = this->tableWidget->rowCount();

  filenames_ts.resize(nTimeseries);
  colors_ts.resize(nTimeseries);
  units_ts.resize(nTimeseries);
  seriesname_ts.resize(nTimeseries);
  xshift_ts.resize(nTimeseries);
  yshift_ts.resize(nTimeseries);
  date_ts.resize(nTimeseries);
  stationfile_ts.resize(nTimeseries);
  filetype_ts.resize(nTimeseries);
  checkStates_ts.resize(nTimeseries);
  epsg_ts.resize(nTimeseries);
  dflowvar_ts.resize(nTimeseries);
  dflowlayer_ts.resize(nTimeseries);

  for (i = 0; i < nTimeseries; i++) {
    filenames_ts[i] = this->tableWidget->item(i, 6)->text();
    seriesname_ts[i] = this->tableWidget->item(i, 1)->text();
    colors_ts[i] = this->tableWidget->item(i, 2)->text();
    units_ts[i] = this->tableWidget->item(i, 3)->text().toDouble();
    xshift_ts[i] = this->tableWidget->item(i, 4)->text().toDouble();
    yshift_ts[i] = this->tableWidget->item(i, 5)->text().toDouble();
    date_ts[i] = this->tableWidget->item(i, 7)->text();
    filetype_ts[i] = this->tableWidget->item(i, 8)->text().toInt();
    stationfile_ts[i] = this->tableWidget->item(i, 10)->text();
    if (this->tableWidget->item(i, 0)->checkState() == Qt::Checked)
      checkStates_ts[i] = 1;
    else
      checkStates_ts[i] = 0;
    epsg_ts[i] = this->tableWidget->item(i, 11)->text().toInt();
    dflowvar_ts[i] = this->tableWidget->item(i, 12)->text();
    dflowlayer_ts[i] = this->tableWidget->item(i, 13)->text().toInt();
  }

  ierr = Generic::NETCDF_ERR(nc_def_dim(ncid, "ntimeseries",
                                           static_cast<size_t>(nTimeseries),
                                           &dimid_ntimeseries));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_dim(ncid, "one", 1, &dimid_one));
  if (ierr != NC_NOERR)
    return 1;

  // Arrays
  dims_1d[0] = dimid_ntimeseries;
  ierr = Generic::NETCDF_ERR(nc_def_var(
      ncid, "timeseries_filename", NC_STRING, 1, dims_1d, &varid_filename));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_colors", NC_STRING,
                                           1, dims_1d, &varid_colors));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_names", NC_STRING,
                                           1, dims_1d, &varid_names));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(
      nc_def_var(ncid, "timeseries_filetype", NC_INT, 1, dims_1d, &varid_type));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_coldstartdate",
                                           NC_STRING, 1, dims_1d,
                                           &varid_coldstart));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_stationfile",
                                           NC_STRING, 1, dims_1d,
                                           &varid_stationfile));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_xshift", NC_DOUBLE,
                                           1, dims_1d, &varid_xshift));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_yshift", NC_DOUBLE,
                                           1, dims_1d, &varid_yshift));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_units", NC_DOUBLE,
                                           1, dims_1d, &varid_units));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(
      ncid, "timeseries_checkState", NC_INT, 1, dims_1d, &varid_checkState));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(
      nc_def_var(ncid, "timeseries_epsg", NC_INT, 1, dims_1d, &varid_epsg));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(
      ncid, "timeseries_dflowvar", NC_STRING, 1, dims_1d, &varid_dflowvar));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_layer", NC_INT, 1,
                                           dims_1d, &varid_dflowlayer));
  if (ierr != NC_NOERR)
    return 1;

  // Scalars
  dims_1d[0] = dimid_one;
  ierr = Generic::NETCDF_ERR(nc_def_var(
      ncid, "timeseries_plottitle", NC_STRING, 1, dims_1d, &varid_plottitle));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_xlabel", NC_STRING,
                                           1, dims_1d, &varid_xlabel));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_ylabel", NC_STRING,
                                           1, dims_1d, &varid_ylabel));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_precision", NC_INT,
                                           1, dims_1d, &varid_precision));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(
      ncid, "timeseries_startdate", NC_STRING, 1, dims_1d, &varid_startdate));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(
      ncid, "timeseries_enddate", NC_STRING, 1, dims_1d, &varid_enddate));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(
      nc_def_var(ncid, "timeseries_ymin", NC_DOUBLE, 1, dims_1d, &varid_ymin));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(
      nc_def_var(ncid, "timeseries_ymax", NC_DOUBLE, 1, dims_1d, &varid_ymax));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_def_var(ncid, "timeseries_autodate", NC_INT,
                                           1, dims_1d, &varid_autodate));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(
      nc_def_var(ncid, "timeseries_autoy", NC_INT, 1, dims_1d, &varid_autoy));
  if (ierr != NC_NOERR)
    return 1;
  ierr = Generic::NETCDF_ERR(nc_enddef(ncid));
  if (ierr != NC_NOERR)
    return 1;

  tempByte = this->plotTitleWidget->text().toUtf8();
  mydatastring[0] = tempByte.data();
  ierr = Generic::NETCDF_ERR(
      nc_put_var_string(ncid, varid_plottitle, mydatastring));
  if (ierr != NC_NOERR)
    return 1;

  tempByte = this->xLabelWidget->text().toUtf8();
  mydatastring[0] = tempByte.data();
  ierr = Generic::NETCDF_ERR(
      nc_put_var_string(ncid, varid_xlabel, mydatastring));
  if (ierr != NC_NOERR)
    return 1;

  tempByte = this->yLabelWidget->text().toUtf8();
  mydatastring[0] = tempByte.data();
  ierr = Generic::NETCDF_ERR(
      nc_put_var_string(ncid, varid_ylabel, mydatastring));
  if (ierr != NC_NOERR)
    return 1;

  tempByte =
      this->startDateEdit->dateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8();
  mydatastring[0] = tempByte.data();
  ierr = Generic::NETCDF_ERR(
      nc_put_var_string(ncid, varid_startdate, mydatastring));
  if (ierr != NC_NOERR)
    return 1;

  tempByte =
      this->endDateEdit->dateTime().toString("yyyy-MM-dd hh:mm:ss").toUtf8();
  mydatastring[0] = tempByte.data();
  ierr = Generic::NETCDF_ERR(
      nc_put_var_string(ncid, varid_enddate, mydatastring));
  if (ierr != NC_NOERR)
    return 1;

  mydataint[0] = 3;
  ierr =
      Generic::NETCDF_ERR(nc_put_var_int(ncid, varid_precision, mydataint));
  if (ierr != NC_NOERR)
    return 1;
  mydatadouble[0] = this->yMinSpinBox->value();
  ierr =
      Generic::NETCDF_ERR(nc_put_var_double(ncid, varid_ymin, mydatadouble));
  if (ierr != NC_NOERR)
    return 1;
  mydatadouble[0] = this->yMaxSpinBox->value();
  ierr =
      Generic::NETCDF_ERR(nc_put_var_double(ncid, varid_ymax, mydatadouble));
  if (ierr != NC_NOERR)
    return 1;

  if (this->checkAllData->isChecked())
    mydataint[0] = 1;
  else
    mydataint[0] = 0;
  ierr =
      Generic::NETCDF_ERR(nc_put_var_int(ncid, varid_autodate, mydataint));
  if (ierr != NC_NOERR)
    return 1;

  if (this->checkYAuto->isChecked())
    mydataint[0] = 1;
  else
    mydataint[0] = 0;
  ierr = Generic::NETCDF_ERR(nc_put_var_int(ncid, varid_autoy, mydataint));
  if (ierr != NC_NOERR)
    return 1;

  for (iu = 0; iu < static_cast<unsigned int>(nTimeseries); iu++) {
    start[0] = iu;

    relPath = CurrentDir.relativeFilePath(filenames_ts[(int)iu]);
    tempByte = relPath.toUtf8();
    mydatastring[0] = tempByte.data();
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_string(ncid, varid_filename, start, mydatastring));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    tempString = seriesname_ts[(int)iu];
    tempByte = tempString.toUtf8();
    mydatastring[0] = tempByte.data();
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_string(ncid, varid_names, start, mydatastring));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    tempByte = colors_ts[(int)iu].toUtf8();
    mydatastring[0] = tempByte.data();
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_string(ncid, varid_colors, start, mydatastring));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    tempByte = date_ts[(int)iu].toUtf8();
    mydatastring[0] = tempByte.data();
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_string(ncid, varid_coldstart, start, mydatastring));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    relPath = CurrentDir.relativeFilePath(stationfile_ts[(int)iu]);
    tempByte = relPath.toUtf8();
    mydatastring[0] = tempByte.data();
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_string(ncid, varid_stationfile, start, mydatastring));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    mydataint[0] = filetype_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_int(ncid, varid_type, start, mydataint));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    mydatadouble[0] = xshift_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_double(ncid, varid_xshift, start, mydatadouble));
    if (ierr != NC_NOERR)
      return 1;

    mydatadouble[0] = yshift_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_double(ncid, varid_yshift, start, mydatadouble));
    if (ierr != NC_NOERR)
      return 1;

    mydatadouble[0] = units_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_double(ncid, varid_units, start, mydatadouble));
    if (ierr != NC_NOERR)
      return 1;

    mydataint[0] = checkStates_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_int(ncid, varid_checkState, start, mydataint));
    if (ierr != NC_NOERR)
      return 1;

    mydataint[0] = epsg_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_int(ncid, varid_epsg, start, mydataint));
    if (ierr != NC_NOERR)
      return 1;

    tempByte = dflowvar_ts[(int)iu].toUtf8();
    mydatastring[0] = tempByte.data();
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_string(ncid, varid_dflowvar, start, mydatastring));
    if (ierr != NC_NOERR)
      return 1;
    mydatastring[0] = NULL;

    mydataint[0] = dflowlayer_ts[(int)iu];
    ierr = Generic::NETCDF_ERR(
        nc_put_var1_int(ncid, varid_dflowlayer, start, mydataint));
    if (ierr != NC_NOERR)
      return 1;
  }

  ierr = Generic::NETCDF_ERR(nc_close(ncid));
  if (ierr != NC_NOERR)
    return 1;
  return 0;
}

int Session::open(QString openFilename) {
  int ierr, ncid, i, nrow;
  int dimid_ntimeseries, nTimeseries;
  int varid_filename, varid_colors, varid_units, varid_names;
  int varid_xshift, varid_yshift, varid_type, varid_coldstart;
  int varid_stationfile, varid_plottitle, varid_xlabel, varid_ylabel;
  int varid_startdate, varid_enddate, varid_precision, varid_ymin, varid_ymax;
  int varid_autodate, varid_autoy, varid_checkState, varid_epsg, varid_dflowvar;
  int varid_dflowlayer;
  const char *mydatachar[1];
  double mydatadouble[1];
  int mydataint[1];
  int type;
  int epsg, layer;
  QMessageBox::StandardButton reply;
  QString filelocation, filename, series_name, color;
  QString coldstartstring, stationfile, stationfilepath;
  QString BaseFile, NewFile, TempFile, BaseDir;
  QString dflowvar;
  double unitconvert, xshift, yshift;
  size_t temp_size_t;
  size_t start[1];
  QDate tempstartdate, tempenddate;
  QString tempstring;
  QColor CellColor;
  QDateTime ColdStart;
  bool continueToLoad, hasCheckInfo;
  Qt::CheckState checkState;

  QFile Session(openFilename);
  if (!Session.exists()) {
    emit sessionError("File not found");
    return 1;
  }

  // Open the netCDF file
  ierr =
      Generic::NETCDF_ERR(nc_open(openFilename.toUtf8(), NC_NOWRITE, &ncid));
  if (ierr != NC_NOERR)
    return 1;

  // Read some of the basics from the file (dimensions, variable IDs)
  ierr = Generic::NETCDF_ERR(
      nc_inq_dimid(ncid, "ntimeseries", &dimid_ntimeseries));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_filename", &varid_filename));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_colors", &varid_colors));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_names", &varid_names));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_filetype", &varid_type));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_units", &varid_units));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_xshift", &varid_xshift));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_yshift", &varid_yshift));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_coldstartdate", &varid_coldstart));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_stationfile", &varid_stationfile));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_plottitle", &varid_plottitle));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_xlabel", &varid_xlabel));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_ylabel", &varid_ylabel));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_precision", &varid_precision));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_startdate", &varid_startdate));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_enddate", &varid_enddate));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_ymin", &varid_ymin));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_ymax", &varid_ymax));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_autodate", &varid_autodate));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_autoy", &varid_autoy));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_epsg", &varid_epsg));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_dflowvar", &varid_dflowvar));
  if (ierr != NC_NOERR)
    return 1;

  ierr = Generic::NETCDF_ERR(
      nc_inq_varid(ncid, "timeseries_layer", &varid_dflowlayer));
  if (ierr != NC_NOERR)
    return 1;

  ierr = nc_inq_varid(ncid, "timeseries_checkState", &varid_checkState);
  if (ierr != NC_NOERR)
    hasCheckInfo = false;
  else
    hasCheckInfo = true;

  // Read the scalar variables
  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_plottitle, &mydatachar));
  if (ierr != NC_NOERR)
    return 1;
  this->plotTitleWidget->setText(QString(mydatachar[0]));

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_xlabel, &mydatachar));
  if (ierr != NC_NOERR)
    return 1;
  this->xLabelWidget->setText(QString(mydatachar[0]));

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_ylabel, &mydatachar));
  if (ierr != NC_NOERR)
    return 1;
  this->yLabelWidget->setText(QString(mydatachar[0]));

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_startdate, &mydatachar));
  if (ierr != NC_NOERR)
    return 1;
  tempstring = QString(mydatachar[0]);
  tempstartdate =
      QDateTime::fromString(tempstring, "yyyy-MM-dd hh:mm:ss").date();
  this->startDateEdit->setDate(tempstartdate);

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_enddate, &mydatachar));
  if (ierr != NC_NOERR)
    return 1;
  tempstring = QString(mydatachar[0]);
  tempenddate = QDateTime::fromString(tempstring, "yyyy-MM-dd hh:mm:ss").date();
  this->endDateEdit->setDate(tempenddate);

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_ymin, &mydatadouble));
  if (ierr != NC_NOERR)
    return 1;
  this->yMinSpinBox->setValue(mydatadouble[0]);

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_ymax, &mydatadouble));
  if (ierr != NC_NOERR)
    return 1;
  this->yMaxSpinBox->setValue(mydatadouble[0]);

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_autodate, &mydataint));
  if (ierr != NC_NOERR)
    return 1;
  if (mydataint[0] == 0)
    this->checkAllData->setChecked(false);
  else
    this->checkAllData->setChecked(true);

  ierr = Generic::NETCDF_ERR(nc_get_var(ncid, varid_autoy, &mydataint));
  if (ierr != NC_NOERR)
    return 1;
  if (mydataint[0] == 0)
    this->checkYAuto->setChecked(false);
  else
    this->checkYAuto->setChecked(true);

  // Next, read in the data and add rows to the table
  ierr = Generic::NETCDF_ERR(
      nc_inq_dimlen(ncid, dimid_ntimeseries, &temp_size_t));
  if (ierr != NC_NOERR)
    return 1;
  nTimeseries = static_cast<int>(temp_size_t);
  nrow = 0;

  // Get the location we are currently working in
  Generic::splitPath(openFilename, TempFile, this->currentDirectory);

  for (i = 0; i < nTimeseries; i++) {
    start[0] = static_cast<size_t>(i);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_filename, start, &mydatachar));
    if (ierr != NC_NOERR)
      return 1;
    filelocation = QString(mydatachar[0]);
    Generic::splitPath(filelocation, filename, TempFile);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_names, start, &mydatachar));
    if (ierr != NC_NOERR)
      return 1;
    series_name = QString(mydatachar[0]);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_type, start, &mydataint));
    if (ierr != NC_NOERR)
      return 1;
    type = mydataint[0];

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_colors, start, &mydatachar));
    if (ierr != NC_NOERR)
      return 1;
    color = QString(mydatachar[0]);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_units, start, &mydatadouble));
    if (ierr != NC_NOERR)
      return 1;
    unitconvert = mydatadouble[0];

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_coldstart, start, &mydatachar));
    if (ierr != NC_NOERR)
      return 1;
    coldstartstring = QString(mydatachar[0]);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_xshift, start, &mydatadouble));
    if (ierr != NC_NOERR)
      return 1;
    xshift = mydatadouble[0];

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_yshift, start, &mydatadouble));
    if (ierr != NC_NOERR)
      return 1;
    yshift = mydatadouble[0];

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_stationfile, start, &mydatachar));
    if (ierr != NC_NOERR)
      return 1;
    stationfilepath = QString(mydatachar[0]);
    Generic::splitPath(stationfilepath, stationfile, TempFile);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_dflowvar, start, &mydatachar));
    if (ierr != NC_NOERR)
      return 1;
    dflowvar = QString(mydatachar[0]);

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_epsg, start, &mydataint));
    if (ierr != NC_NOERR)
      return 1;
    epsg = mydataint[0];

    ierr = Generic::NETCDF_ERR(
        nc_get_var1(ncid, varid_dflowlayer, start, &mydataint));
    if (ierr != NC_NOERR)
      return 1;
    layer = mydataint[0];

    if (hasCheckInfo) {
      ierr = Generic::NETCDF_ERR(
          nc_get_var1(ncid, varid_checkState, start, &mydataint));
      if (ierr != NC_NOERR)
        return 1;
      if (mydataint[0] == 1)
        checkState = Qt::Checked;
      else
        checkState = Qt::Unchecked;
    } else
      checkState = Qt::Checked;

    continueToLoad = false;

    filelocation = this->currentDirectory + "/" + filelocation;
    Generic::splitPath(filelocation, BaseFile, BaseDir);

    QFile myfile(filelocation);
    if (!myfile.exists()) {
      // The file wasn't found where we think it should be. Give the
      // user a chance to specify an alternate data directory
      if (this->alternateFolder == NULL) {
        // If we haven't previously specified an alternate folder, inform the
        // user and  ask if they want to specify.
        reply =
            QMessageBox::question((QWidget *)parent(), tr("File not found"),
                                  tr("Data file not found in default location. "
                                     "Would you like to specify another?"));
        if (reply == QMessageBox::Yes) {

          // Get an alternate location
          // this->alternateFolder = QFileDialog::getExistingDirectory(0,"Select
          // Folder",this->previousDirectory);
          this->alternateFolder = QFileDialog::getExistingDirectory(
              (QWidget *)parent(), tr("Select Folder"),
              this->previousDirectory);
          NewFile = this->alternateFolder + "/" + BaseFile;
          QFile myfile(NewFile);
          if (!myfile.exists()) {
            continueToLoad = false;
            emit sessionError(tr("The file ") + BaseFile +
                              tr(" was not found and has been skipped."));
          } else {
            continueToLoad = true;
            filelocation = NewFile;
          }
        } else
          continueToLoad = false;
      } else {
        // Start by trying the previously specified alternate folder
        NewFile = this->alternateFolder + "/" + BaseFile;
        QFile myfile(NewFile);
        if (!myfile.exists()) {
          reply =
              QMessageBox::question((QWidget *)parent(), tr("File not found"),
                                    tr("File not found in default location. "
                                       "Would you like to specify another?"));
          if (reply == QMessageBox::Yes) {
            // Get an alternate location
            this->alternateFolder = QFileDialog::getExistingDirectory(
                (QWidget *)parent(), tr("Select Folder"),
                this->previousDirectory);
            NewFile = this->alternateFolder + "/" + BaseFile;
            QFile myfile(NewFile);
            if (!myfile.exists()) {
              continueToLoad = false;
              emit sessionError(tr("The file ") + BaseFile +
                                tr(" was not found and has been skipped."));
            } else {
              continueToLoad = true;
              filelocation = NewFile;
            }
          } else
            continueToLoad = false;
        } else {
          continueToLoad = true;
          filelocation = NewFile;
        }
      }
    } else {
      continueToLoad = true;
    }

    if (type == MetOceanViewer::FileType::ASCII_ADCIRC) {
      Generic::splitPath(stationfilepath, BaseFile, BaseDir);
      stationfilepath = this->currentDirectory + "/" + stationfilepath;
      QFile myfile(stationfilepath);
      if (!myfile.exists()) {
        // The file wasn't found where we think it should be. Give the
        // user a chance to specify an alternate data directory
        if (this->alternateFolder == NULL) {
          // If we haven't previously specified an alternate folder, inform the
          // user and  ask if they want to specify.
          reply = QMessageBox::question(
              (QWidget *)parent(), tr("File not found"),
              tr("Station file not found in default location. Would you like "
                 "to specify another?"));
          if (reply == QMessageBox::Yes) {
            // Get an alternate location
            this->alternateFolder = QFileDialog::getExistingDirectory(
                (QWidget *)parent(), tr("Select Folder"),
                this->previousDirectory);
            NewFile = this->alternateFolder + "/" + BaseFile;
            QFile myfile(NewFile);
            if (!myfile.exists()) {
              continueToLoad = false;
              emit sessionError(tr("The file ") + BaseFile +
                                tr(" was not found and has been skipped."));
            } else {
              continueToLoad = true;
              stationfilepath = NewFile;
            }
          } else
            continueToLoad = false;
        } else {
          // Start by trying the previously specified alternate folder
          NewFile = this->alternateFolder + "/" + BaseFile;
          QFile myfile(NewFile);
          if (!myfile.exists()) {
            reply =
                QMessageBox::question((QWidget *)parent(), tr("File not found"),
                                      tr("File not found in default location. "
                                         "Would you like to specify another?"));
            if (reply == QMessageBox::Yes) {
              // Get an alternate location
              this->alternateFolder = QFileDialog::getExistingDirectory(
                  (QWidget *)parent(), tr("Select Folder"),
                  this->previousDirectory);
              NewFile = this->alternateFolder + "/" + BaseFile;
              QFile myfile(NewFile);
              if (!myfile.exists()) {
                continueToLoad = false;
                emit sessionError(tr("The file ") + BaseFile +
                                  tr(" was not found and has been skipped."));
              } else {
                continueToLoad = true;
                stationfilepath = NewFile;
              }
            } else
              continueToLoad = false;
          } else {
            continueToLoad = true;
            stationfilepath = NewFile;
          }
        }
      } else {
        continueToLoad = true;
      }
    }

    if (continueToLoad) {
      // Build the table
      nrow = nrow + 1;
      this->tableWidget->setRowCount(nrow);
      this->tableWidget->setRowCount(nrow);
      this->tableWidget->setItem(nrow - 1, 0, new QTableWidgetItem(filename));
      this->tableWidget->setItem(nrow - 1, 1,
                                 new QTableWidgetItem(series_name));
      this->tableWidget->setItem(nrow - 1, 2, new QTableWidgetItem(color));
      this->tableWidget->setItem(
          nrow - 1, 3, new QTableWidgetItem(QString::number(unitconvert)));
      this->tableWidget->setItem(nrow - 1, 4,
                                 new QTableWidgetItem(QString::number(xshift)));
      this->tableWidget->setItem(nrow - 1, 5,
                                 new QTableWidgetItem(QString::number(yshift)));
      this->tableWidget->setItem(nrow - 1, 6,
                                 new QTableWidgetItem(filelocation));
      this->tableWidget->setItem(nrow - 1, 7,
                                 new QTableWidgetItem((coldstartstring)));
      this->tableWidget->setItem(nrow - 1, 8,
                                 new QTableWidgetItem(QString::number(type)));
      this->tableWidget->setItem(nrow - 1, 9,
                                 new QTableWidgetItem(stationfile));
      this->tableWidget->setItem(nrow - 1, 10,
                                 new QTableWidgetItem(stationfilepath));
      this->tableWidget->setItem(nrow - 1, 11,
                                 new QTableWidgetItem(QString::number(epsg)));
      this->tableWidget->setItem(nrow - 1, 12, new QTableWidgetItem(dflowvar));
      this->tableWidget->setItem(nrow - 1, 13,
                                 new QTableWidgetItem(QString::number(layer)));
      CellColor.setNamedColor(color);
      this->tableWidget->item(nrow - 1, 2)->setBackgroundColor(CellColor);
      this->tableWidget->item(nrow - 1, 2)->setTextColor(CellColor);
      this->tableWidget->item(nrow - 1, 0)->setCheckState(checkState);
      ColdStart = QDateTime::fromString(coldstartstring, "yyyy-MM-dd hh:mm:ss");
    }
  }
  ierr = Generic::NETCDF_ERR(nc_close(ncid));
  if (ierr != NC_NOERR)
    return 1;

  return 0;
}
