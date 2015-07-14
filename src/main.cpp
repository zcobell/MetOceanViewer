//-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2015  Zach Cobell
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
// The name "MetOcean Viewer" is specific to this project and may not be
// used for projects "forked" or derived from this work.
//
//-----------------------------------------------------------------------//
        
#include <MetOceanViewer.h>
#include <QApplication>

//-------------------------------------------//
//Main function for the code
//-------------------------------------------//
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    if(!isConnectedToNetwork())
    {
        QMessageBox::critical(0,"Internet Connection Not Detected",
                              "No internet connection was detected.\n "
                              "The program will now be terminated.");
        return 1;
    }

    //If the session file was dropped onto the executable,
    //try to load it.
    if(argc==2)
        SessionFile = QCoreApplication::arguments().at(1);

    //Create the window
    MainWindow w;

    //Splash Screen
    QPixmap pixmap(":/rsc/img/logo_full.png");
    if(pixmap.isNull())
    {
        //Warning about splash screen, which shouldn't happen
        QMessageBox::warning(0,"ERROR","Failed to load spash screen image.");
    }
    else
    {
        //Display the splash screen for 2 seconds
        QSplashScreen splash(pixmap,Qt::WindowStaysOnTopHint);
        splash.setEnabled(false);
        splash.show();
        a.processEvents();

        QTime dieTime= QTime::currentTime().addSecs(2);
        while( QTime::currentTime() < dieTime )
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        splash.hide();
    }

    //Show the user the window
    w.show();


    return a.exec();
}
//-------------------------------------------//
