/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2019  Zach Cobell
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
#ifndef GENERIC_H
#define GENERIC_H

#include <QDateTime>
#include <QNetworkInterface>
#include <QString>
#include <QtCore>

class Generic : public QObject {
  Q_OBJECT
 public:
  static void splitPath(QString input, QString &filename, QString &directory);
  static void delay(int delayTime);
  static void delayM(int delayTime);
  static bool isConnectedToNetwork();
  static bool createConfigDirectory();
  static bool createConfigDirectory(QString &configDirectory);
  static QString configDirectory();
  static QString crmsDataFile();

 private:
  static QString dummyConfigDir;
};

#endif  // GENERIC_H
