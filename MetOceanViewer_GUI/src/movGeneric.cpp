/*-------------------------------GPL-------------------------------------//
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
//-----------------------------------------------------------------------*/
#include "movGeneric.h"
#include <QDesktopServices>
#include <QWebEnginePage>
#include <netcdf.h>

// The name of the session file
QString SessionFile;

//-------------------------------------------//
// Simple delay function which will pause
// execution for a number of seconds
//-------------------------------------------//
void MovGeneric::delay(int delayTime) {
  QTime dieTime = QTime::currentTime().addSecs(delayTime);
  while (QTime::currentTime() < dieTime)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  return;
}
//-------------------------------------------//

//-------------------------------------------//
// Simple delay function which will pause
// execution for a number of milliseconds
//-------------------------------------------//
void MovGeneric::delayM(int delayTime) {
  QTime dieTime = QTime::currentTime().addMSecs(delayTime);
  while (QTime::currentTime() < dieTime)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  return;
}
//-------------------------------------------//

void MovGeneric::splitPath(QString input, QString &filename,
                           QString &directory) {
  QRegExp rx("[/\\\\]");
  QStringList parts = input.split(rx);
  filename = parts.value(parts.length() - 1);
  directory = "";
  for (int i = 0; i < parts.length() - 1; i++)
    if (i > 0)
      directory = directory + "/" + parts.value(i);
    else
      directory = parts.value(i);
  return;
}

//-------------------------------------------//
// NetCDF Error function
//-------------------------------------------//
int MovGeneric::NETCDF_ERR(int status) {
  if (status != NC_NOERR)
    QMessageBox::critical(NULL, tr("Error Saving File"),
                          tr(nc_strerror(status)));

  return status;
}
//-------------------------------------------//

//-------------------------------------------//
// Function that checks if the computer is
// connected to the network. This is set as a
// prerequisite for running the program since
// every page is Google Maps based, and requires
// a connection to the internet
//-------------------------------------------//
bool MovGeneric::isConnectedToNetwork() {

  QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
  bool result = false;

  for (int i = 0; i < ifaces.count(); i++) {

    QNetworkInterface iface = ifaces.at(i);
    if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
        !iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {

#ifdef DEBUG
      // details of connection
      qDebug() << "name:" << iface.name() << endl
               << "ip addresses:" << endl
               << "mac:" << iface.hardwareAddress() << endl;
#endif

      for (int j = 0; j < iface.addressEntries().count(); j++) {
#ifdef DEBUG
        qDebug() << iface.addressEntries().at(j).ip().toString() << " / "
                 << iface.addressEntries().at(j).netmask().toString() << endl;
#endif

        // got an interface which is up, and has an ip address
        if (result == false)
          result = true;
      }
    }
  }

  return result;
}

int MovGeneric::getLocalTimzoneOffset() {
  QDateTime dt1 = QDateTime::currentDateTime();
  QDateTime dt2 = dt1.toUTC();
  dt1.setTimeSpec(Qt::UTC);
  return dt2.secsTo(dt1) / 3600;
}
