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
#ifndef GENERAL_FUNCTIONS_H
#define GENERAL_FUNCTIONS_H

#include <QMessageBox>
#include <QColor>
#include <QtCore>
#include <QString>
#include <QDateTime>
#include <QNetworkInterface>

void splitPath(QString input,QString &filename,QString &directory);
void delay(int delayTime);
void delayM(int delayTime);
QString MakeColorString(QColor InputColor);
QColor GenerateRandomColor();
QColor styleSheetToColor(QString stylesheet);
int NETCDF_ERR(int status);
bool isConnectedToNetwork();

#endif // GENERAL_FUNCTIONS_H
