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
#include <netcdf.h>

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
