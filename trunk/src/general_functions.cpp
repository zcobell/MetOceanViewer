//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2013.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation version 3.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// contact: Zachary Cobell, zachary.cobell@arcadis-us.com
// ARCADIS
// 4999 Pearl East Circle, Suite 200
// Boulder, CO 80301
//
// All indications and logos of, and references to, "ARCADIS"
// are registered trademarks of ARCADIS, and remain the property of
// ARCADIS. All rights reserved.
//
//------------------------------------------------------------------------------
// $Author$
// $Date$
// $Id$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: general_functions.cpp
//
//------------------------------------------------------------------------------

#include <ADCvalidator.h>
#include <ui_ADCvalidator_main.h>
#include <about_dialog.h>
#include <netcdf.h>

QString SessionFile;

//Simple delay function which will pause execution for a number of seconds
void MainWindow::delay(int delayTime)
{
    QTime dieTime= QTime::currentTime().addSecs(delayTime);
    while( QTime::currentTime() < dieTime )
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

//Routine to remove the leading path of a filename for a simpler
// display in a text box
QString MainWindow::RemoveLeadingPath(QString Input)
{
    QRegExp rx("[/\\\\]");
    QStringList parts = Input.split(rx);
    QString Output = parts.value(parts.length()-1);
    return Output;
}

//Gets the leading path from the directory that is worked in, used for staying
//in the same directory for subsequent selections
void MainWindow::GetLeadingPath(QString Input)
{
    QRegExp rx("[/\\\\]");
    QStringList parts = Input.split(rx);
    QString Directory = "";
    for(int i=0; i<parts.length()-1; i++)
    {
        if(i>0)
            Directory = Directory+"/"+parts.value(i);
        else
            Directory = parts.value(i);

    }
    PreviousDirectory = Directory;

    return;
}

QString MainWindow::GetMyLeadingPath(QString Input)
{
    QRegExp rx("[/\\\\]");
    QStringList parts = Input.split(rx);
    QString Directory = "";
    for(int i=0; i<parts.length()-1; i++)
    {
        if(i>0)
            Directory = Directory+"/"+parts.value(i);
        else
            Directory = parts.value(i);

    }
    return Directory;
}

//Makes a string that sets the CSS style according to the input color
QString MainWindow::MakeColorString(QColor InputColor)
{
    QString S("background-color: #"
                + QString(InputColor.red() < 16? "0" : "") + QString::number(InputColor.red(),16)
                + QString(InputColor.green() < 16? "0" : "") + QString::number(InputColor.green(),16)
                + QString(InputColor.blue() < 16? "0" : "") + QString::number(InputColor.blue(),16) + ";");
    return S;
}

//Terminates the application when quit button clicked
void MainWindow::on_actionQuit_triggered()
{
    close();
}

//Generates a random color and mixes in white to make it
//more aestetically pleasing
QColor MainWindow::GenerateRandomColor()
{
    QColor MyColor, Mix;
    QTime SeedTime;
    bool DoMix;

    DoMix = false;

    SeedTime = QTime::currentTime();
    qsrand((uint)SeedTime.msec());

    if(DoMix)
    {
        Mix.setRed(255);
        Mix.setGreen(255);
        Mix.setBlue(255);

        MyColor.setRed((qrand()%255 + Mix.red()) / 2);
        MyColor.setGreen((qrand()%255 + Mix.green()) / 2);
        MyColor.setBlue((qrand()%255 + Mix.blue()) / 2);
    }
    else
    {
        MyColor.setRed(qrand()%255);
        MyColor.setGreen(qrand()%255);
        MyColor.setBlue(qrand()%255);
    }

    return MyColor;
}

//NetCDF Error function
void MainWindow::NETCDF_ERR(int status)
{
    QMessageBox::information(this,"ERROR",nc_strerror(status));
    return;
}

//Handle the unsupported content, ie the image that comes back
//from the highcharts export server as a png file. Save using
//a file save dialog window to select the save location/filename
void MainWindow::unsupportedContent(QNetworkReply * reply)
{

    QApplication::setOverrideCursor(Qt::WaitCursor);

    //Wait until we've retrieved the entire image
    while(!reply->isFinished())
    {
        delay(1);
    }

    //Make an image and load the reply from the web
    QImage* highchart = new QImage();
    highchart->loadFromData(reply->readAll());

    QApplication::restoreOverrideCursor();

    //Request the save location
    QString Filename = QFileDialog::getSaveFileName(this,"Save as...",
                PreviousDirectory,"Portable Network Graphics (*.png)");

    //Make sure user didn't click 'Cancel'
    if(Filename==NULL)
        return;

    //Get leading path and save so we open in same place next time
    GetLeadingPath(Filename);

    //Save to disk
    highchart->save(Filename);

    return;
}

//Handle the "enter" or "return" keypress events on certain
//pages to automatically draw plots
void MainWindow::keyPressEvent(QKeyEvent *key)
{
    if(ui->MainTabs->currentIndex()==1)
    {
        if(ui->subtab_IMEDS->currentIndex()==1)
        {
            if(key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)
            {
                on_button_plotStation_clicked();
            }
        }
    }
    else if(ui->MainTabs->currentIndex()==0)
    {
        if(key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)
        {
            on_Button_FetchData_clicked();
        }
    }
    return;
}

bool isConnectedToNetwork(){

    QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    bool result = false;

    for (int i = 0; i < ifaces.count(); i++) {

        QNetworkInterface iface = ifaces.at(i);
        if ( iface.flags().testFlag(QNetworkInterface::IsUp)
             && !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {

#ifdef DEBUG
            // details of connection
            qDebug() << "name:" << iface.name() << endl
                     << "ip addresses:" << endl
                     << "mac:" << iface.hardwareAddress() << endl;
#endif


            for (int j=0; j<iface.addressEntries().count(); j++) {
#ifdef DEBUG
                qDebug() << iface.addressEntries().at(j).ip().toString()
                         << " / " << iface.addressEntries().at(j).netmask().toString() << endl;
#endif

                // got an interface which is up, and has an ip address
                if (result == false)
                    result = true;
            }
        }

    }

    return result;
}

void MainWindow::on_actionAbout_triggered()
{
    about_dialog aboutWindow;
    aboutWindow.setModal(false);
    aboutWindow.exec();
    return;
}

void MainWindow::on_actionLoad_Session_triggered()
{
    QString LoadFile = QFileDialog::getOpenFileName(this,"Open Session...",PreviousDirectory,"ADCIRC Validator Sessions (*.avs)");

    if(LoadFile==NULL)
        return;

    SessionFile = LoadFile;

    QMessageBox::information(this,"Not yet...","Feature not yet implemented.");
    return;
}

void MainWindow::on_actionSave_Session_triggered()
{
    saveSession();
    return;
}

void MainWindow::on_actionSave_Session_As_triggered()
{
    QString SaveFile = QFileDialog::getSaveFileName(this,"Save Session...",PreviousDirectory,"ADCIRC Validatior Sessions (*.avs)");
    if(SaveFile!=NULL)
    {
        SessionFile = SaveFile;
        saveSession();
    }
    return;
}

int MainWindow::saveSession()
{
    int ierr,ncid,i;
    int dimid_ntimeseries;
    int varid_filename,varid_colors,varid_units,varid_names;
    int varid_xshift,varid_yshift,varid_type,varid_coldstart;
    int varid_stationfile;
    int dims_1d[1];
    int nTimeseries;
    unsigned int start[1];
    unsigned int count[1];
    unsigned int iu;
    double mydatadouble[1];
    const char * mydatastring[1];

    QFile Session(SessionFile);

    QVector<QString> filenames_ts;
    QVector<QString> filetype_ts;
    QVector<QString> colors_ts;
    QVector<double> units_ts;
    QVector<QString> seriesname_ts;
    QVector<double> xshift_ts;
    QVector<double> yshift_ts;
    QVector<QString> date_ts;
    QVector<QString> stationfile_ts;

    if(SessionFile==NULL)
    {
        QString SaveFile = QFileDialog::getSaveFileName(this,"Save Session...",PreviousDirectory,"ADCIRC Validatior Sessions (*.avs)");
        if(SaveFile==NULL)
            return 0;
    }

    if(Session.exists())
        Session.remove();

    ierr = nc_create(SessionFile.toStdString().c_str(),NC_NETCDF4,&ncid);

    //Start setting up the definitions
    nTimeseries = ui->table_IMEDSData->rowCount();

    filenames_ts.resize(nTimeseries);
    colors_ts.resize(nTimeseries);
    units_ts.resize(nTimeseries);
    seriesname_ts.resize(nTimeseries);
    xshift_ts.resize(nTimeseries);
    yshift_ts.resize(nTimeseries);
    date_ts.resize(nTimeseries);
    stationfile_ts.resize(nTimeseries);
    filetype_ts.resize(nTimeseries);

    for(i=0;i<nTimeseries;i++)
    {
        filenames_ts[i] = ui->table_IMEDSData->item(i,0)->text();
        seriesname_ts[i] = ui->table_IMEDSData->item(i,1)->text();
        colors_ts[i] = ui->table_IMEDSData->item(i,2)->text();
        units_ts[i] = ui->table_IMEDSData->item(i,3)->text().toDouble();
        xshift_ts[i] = ui->table_IMEDSData->item(i,4)->text().toDouble();
        yshift_ts[i] = ui->table_IMEDSData->item(i,5)->text().toDouble();
        date_ts[i] = ui->table_IMEDSData->item(i,7)->text();
        filetype_ts[i] = ui->table_IMEDSData->item(i,8)->text();
        stationfile_ts[i] = ui->table_IMEDSData->item(i,10)->text();
    }

    ierr = nc_def_dim(ncid,"ntimeseries",static_cast<size_t>(nTimeseries),&dimid_ntimeseries);

    dims_1d[0] = dimid_ntimeseries;
    ierr = nc_def_var(ncid,"timeseries_filename",NC_STRING,1,dims_1d,&varid_filename);
    ierr = nc_def_var(ncid,"timeseries_colors",NC_STRING,1,dims_1d,&varid_colors);
    ierr = nc_def_var(ncid,"timeseries_names",NC_STRING,1,dims_1d,&varid_names);
    ierr = nc_def_var(ncid,"timeseries_filetype",NC_STRING,1,dims_1d,&varid_type);
    ierr = nc_def_var(ncid,"timeseries_coldstartdate",NC_STRING,1,dims_1d,&varid_coldstart);
    ierr = nc_def_var(ncid,"timeseries_stationfile",NC_STRING,1,dims_1d,&varid_coldstart);
    ierr = nc_def_var(ncid,"timeseries_units",NC_DOUBLE,1,dims_1d,&varid_units);
    ierr = nc_def_var(ncid,"timeseries_xshift",NC_DOUBLE,1,dims_1d,&varid_xshift);
    ierr = nc_def_var(ncid,"timeseries_yshift",NC_DOUBLE,1,dims_1d,&varid_yshift);

    ierr = nc_enddef(ncid);

    for(iu=0;iu<static_cast<unsigned int>(nTimeseries);iu++)
    {
        start[0] = iu;
        count[0] = 1;

        mydatastring[0]  = filenames_ts[0].toStdString().c_str();
        ierr  = nc_put_var1_string(ncid,varid_filename,start,mydatastring);

        mydatastring[0]  = seriesname_ts[0].toStdString().c_str();
        ierr  = nc_put_var1_string(ncid,varid_names,start,mydatastring);

        mydatastring[0]  = colors_ts[0].toStdString().c_str();
        ierr  = nc_put_var1_string(ncid,varid_colors,start,mydatastring);

        mydatastring[0]  = date_ts[0].toStdString().c_str();
        ierr  = nc_put_var1_string(ncid,varid_coldstart,start,mydatastring);

        mydatastring[0]  = stationfile_ts[0].toStdString().c_str();
        ierr  = nc_put_var1_string(ncid,varid_stationfile,start,mydatastring);

        mydatastring[0]  = filetype_ts[0].toStdString().c_str();
        ierr  = nc_put_var1_string(ncid,varid_type,start,mydatastring);

        mydatadouble[0]  = xshift_ts[0];
        ierr  = nc_put_var1_double(ncid,varid_xshift,start,mydatadouble);

        mydatadouble[0]  = yshift_ts[0];
        ierr  = nc_put_var1_double(ncid,varid_yshift,start,mydatadouble);

        mydatadouble[0]  = units_ts[0];
        ierr  = nc_put_var1_double(ncid,varid_units,start,mydatadouble);

    }

    ierr = nc_close(ncid);

    return 0;
}
