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
#ifndef MOV_GENERIC_H
#define MOV_GENERIC_H

#include <QMessageBox>
#include <QColor>
#include <QtCore>
#include <QString>
#include <QDateTime>
#include <QNetworkInterface>

class mov_generic : public QObject
{

public:
    static void splitPath(QString input,QString &filename,QString &directory);
    static void delay(int delayTime);
    static void delayM(int delayTime);
    static QString MakeColorString(QColor InputColor);
    static QColor GenerateRandomColor();
    static QColor styleSheetToColor(QString stylesheet);
    static int NETCDF_ERR(int status);
    static bool isConnectedToNetwork();
    static int getLocalTimzoneOffset();

};

#endif // MOV_GENERIC_H
