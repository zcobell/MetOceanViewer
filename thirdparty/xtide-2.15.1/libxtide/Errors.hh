// $Id: Errors.hh 6184 2016-01-11 01:47:14Z flaterco $

// Enums for errors.

/*
    Copyright (C) 2006  David Flater.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

namespace libxtide {

namespace Error {

  enum TideError {YEAR_OUT_OF_RANGE,
                  MKTIME_FAILED,
                  TIMESTAMP_OVERFLOW,
                  YEAR_NOT_IN_TABLE,
                  NO_HFILE_PATH,
                  NO_HFILE_IN_PATH,
                  IMPOSSIBLE_CONVERSION,
                  NO_CONVERSION,
                  UNRECOGNIZED_UNITS,
                  BOGUS_COORDINATES,
                  CANT_OPEN_FILE,
                  CORRUPT_HARMONICS_FILE,
                  BADCOLORSPEC,
                  XPM_ERROR,
                  NOHOMEDIR,
                  BADHHMM,
                  XMLPARSE,
                  STATION_NOT_FOUND,
                  CANTOPENDISPLAY,
                  NOT_A_NUMBER,
                  PNG_WRITE_FAILURE,
                  CANT_GET_SOCKET,
                  SYSTEMD_NO_SOCKET,
                  CANT_GET_HOST,
                  ABSURD_OFFSETS,
                  NUMBER_RANGE_ERROR,
                  BAD_MODE,
                  BAD_FORMAT,
                  BAD_GRAPHSTYLE,
                  BAD_TIMESTAMP,
                  BAD_BOOL,
                  BAD_TEXT,
                  BAD_EVENTMASK,
                  BAD_OR_AMBIGUOUS_COMMAND_LINE,
                  CANT_LOAD_FONT,
                  NO_SYSLOG,
                  BROKEN_ZONEINFO,
                  ADDRESS_FAIL,
                  PORT_FAIL};

  enum ErrType {fatal, nonfatal};

}

}
