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
#ifndef MOVERRORS_H
#define MOVERRORS_H

#include <QMap>
#include <QObject>

#define ERR_NOERR 0
#define ERR_NOAA_INVALIDDATERANGE -9990
#define ERR_USGS_SERVERREADERROR -8000
#define ERR_USGS_ARCHIVEONLY -8001
#define ERR_USGS_READDATA -8002
#define ERR_USGS_FORMATTING -8003
#define ERR_CANNOT_OPEN_FILE -8004
#define ERR_WRONG_NUMBER_OF_STATIONS -8005
#define ERR_NETCDF -8006
#define ERR_NO_VARIABLE_FOUND -8007
#define ERR_DFLOW_GETPLOTVARS -8008
#define ERR_DFLOW_GETSTATIONS -8009
#define ERR_DFLOW_NOXVELOCITY -8010
#define ERR_DFLOW_NOYVELOCITY -8011
#define ERR_DFLOW_NOZVELOCITY -8012
#define ERR_DFLOW_3DVARS -8013
#define ERR_DFLOW_VARNOTFOUND -8014
#define ERR_DFLOW_ILLEGALDIMENSION -8015
#define ERR_DFLOW_FILEREADERROR -8016
#define ERR_ADCIRC_ASCIIREADERROR -8017
#define ERR_ADCIRC_NETCDFREADERROR -8018
#define ERR_IMEDS_FILEREADERROR -8019
#define ERR_INVALIDFILEFORMAT -8020
#define ERR_GENERICFILEREADERROR -8021
#define ERR_ADCIRC_ASCIITOIMEDS -8022
#define ERR_ADCIRC_NETCDFTOIMEDS -8023
#define ERR_BUILDSTATIONLIST -8024
#define ERR_BUILDREVISEDIMEDS -8025
#define ERR_PROJECTSTATIONS -8026
#define ERR_MARKERSELECTION -8027

class movErrors : public QObject {
  Q_OBJECT
public:
  explicit movErrors(QObject *parent = 0);

  QString toString();

  int errorCode();

  int ncError();

  void setErrorCode(int ierr);

  void setNcErrorCode(int ncerr);

  bool isError();

  bool isNcError();

private:
  int _error;
  int _ncerr;
};

#endif // MOVERRORS_H
