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
//  File: usgs_functions.cpp
//
//------------------------------------------------------------------------------

#include "ADCvalidator.h"
#include "ui_ADCvalidator_main.h"

void MainWindow::ReadUSGSDataFinished(QNetworkReply *reply)
{
    QString Error;
    if(reply->error()!=0)
    {
        QMessageBox::information(this,"ERROR","ERROR: "+reply->errorString());
        return;
    }
    QByteArray RawUSGSData = reply->readAll();
    QString USGSData = FormatUSGSResponse(RawUSGSData,Error);
    reply->deleteLater();
    return;
}

QString MainWindow::FormatUSGSResponse(QByteArray Input,QString &ErrorString)
{
    int i;
    QString Output,TempData,DateS,YearS,MonthS,DayS,HourMinS,HourS,MinS,WLS;
    QString InputData(Input);
    QStringList DataList = InputData.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    QStringList TimeSnap;
    QString Temp(Input);
    ErrorString = Temp;
    return InputData;
}
