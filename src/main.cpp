//---- LGPL --------------------------------------------------------------------
//
// Copyright (C)  ARCADIS, 2011-2015.
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
// $Rev$
// $HeadURL$
//------------------------------------------------------------------------------
//  File: main.cpp
//
//------------------------------------------------------------------------------
        
#include <MetOceanViewer.h>
#include <QApplication>

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

    MainWindow w;
    w.show();
    return a.exec();
}
