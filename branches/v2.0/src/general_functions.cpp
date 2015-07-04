//----GPL-----------------------------------------------------------------------
//
// This file is part of MetOceanViewer.
//
//    MetOceanViewer is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    MetOceanViewer is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with MetOceanViewer.  If not, see <http://www.gnu.org/licenses/>.
//------------------------------------------------------------------------------

#include <MetOceanViewer.h>
#include <ui_MetOceanViewer_main.h>
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
int MainWindow::NETCDF_ERR(int status)
{
    if(status != NC_NOERR)
        QMessageBox::critical(this,"Error Saving File",nc_strerror(status));

    return status;
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

    //Catch "ENTER" or "RETURN" Key
    if(key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)
    {
        //Events for "ENTER" on the Live Data tabs
        if(ui->MainTabs->currentIndex()==0)
        {
            //NOAA Tab
            if(ui->subtab_livedata->currentIndex()==0)
            {
                if(ui->Combo_NOAAPanTo->hasFocus())
                    ui->noaa_map->page()->mainFrame()->evaluateJavaScript("panTo('"+ui->Combo_NOAAPanTo->currentText()+"')");
                else
                    on_Button_FetchData_clicked();
            }
            //USGS Tab
            else if(ui->subtab_livedata->currentIndex()==1)
            {
                if(ui->combo_usgs_panto->hasFocus())
                    ui->usgs_map->page()->mainFrame()->evaluateJavaScript("panTo('"+ui->combo_usgs_panto->currentText()+"')");
                else
                    on_button_usgs_fetch_clicked();
            }
        }
        //Events for "ENTER" on the timeseries tabs
        else if(ui->MainTabs->currentIndex()==1)
        {
           if(ui->subtab_timeseries->currentIndex()==0)
           {
               on_button_processTimeseriesData_clicked();
           }
           else if(ui->subtab_timeseries->currentIndex()==1)
           {
               on_button_plotTimeseriesStation_clicked();
           }
        }
        else if(ui->MainTabs->currentIndex()==2)
        {
            if(ui->subtab_hwm->currentIndex()==1)
                on_button_processHWM_clicked();
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
    QString LoadFile = QFileDialog::getOpenFileName(this,"Open Session...",PreviousDirectory,"MetOceanViewer Sessions (*.mvs, *.avs)");

    if(LoadFile==NULL)
        return;

    SessionFile = LoadFile;

    loadSession();

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
