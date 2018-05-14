/*-------------------------------GPL-------------------------------------//
//
// MetOcean Viewer - A simple interface for viewing hydrodynamic model data
// Copyright (C) 2018  Zach Cobell
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
#include "generic.h"
#include <netcdf.h>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// The name of the session file
QString SessionFile;

//-------------------------------------------//
// Simple delay function which will pause
// execution for a number of seconds
//-------------------------------------------//
void Generic::delay(int delayTime) {
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
void Generic::delayM(int delayTime) {
  QTime dieTime = QTime::currentTime().addMSecs(delayTime);
  while (QTime::currentTime() < dieTime)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  return;
}
//-------------------------------------------//

void Generic::splitPath(QString input, QString &filename, QString &directory) {
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
int Generic::NETCDF_ERR(int status) {
  if (status != NC_NOERR)
    QMessageBox::critical(nullptr, tr("Error Saving File"),
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
bool Generic::isConnectedToNetwork() {
  QNetworkAccessManager nam;
  QNetworkRequest req(QUrl("http://www.google.com"));
  QNetworkReply *reply = nam.get(req);
  QEventLoop loop;
  connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
  loop.exec();
  if (reply->bytesAvailable())
    return true;
  else
    return false;
}

int Generic::getLocalTimzoneOffset() {
  QDateTime dt1 = QDateTime::currentDateTime();
  QDateTime dt2 = dt1.toUTC();
  dt1.setTimeSpec(Qt::UTC);
  return dt2.secsTo(dt1) / 3600;
}
