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

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"

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

QString MainWindow::MakeColorString(QColor InputColor)
{
    QString S("background-color: #"
                + QString(InputColor.red() < 16? "0" : "") + QString::number(InputColor.red(),16)
                + QString(InputColor.green() < 16? "0" : "") + QString::number(InputColor.green(),16)
                + QString(InputColor.blue() < 16? "0" : "") + QString::number(InputColor.blue(),16) + ";");
    return S;
}

