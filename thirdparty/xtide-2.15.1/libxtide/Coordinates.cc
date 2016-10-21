// $Id: Coordinates.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  Coordinates   Degrees latitude and longitude.

    Copyright (C) 1998  David Flater.

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

#include "libxtide.hh"
namespace libxtide {


Coordinates::Coordinates() {}


Coordinates::Coordinates (double lat, double lng):
  Nullable(false),
  latitude(lat),
  longitude(lng) {
  if (lat < -90.0 || lat > 90.0 || lng < -180.0 || lng > 180.0) {
    Dstr details ("The offending coordinates were (");
    details += lat;
    details += ',';
    details += lng;
    details += ").";
    Global::barf (Error::BOGUS_COORDINATES, details);
  }
}


const double Coordinates::lat() const {
  assert (!_isNull);
  return latitude;
}


const double Coordinates::lng() const {
  assert (!_isNull);
  return longitude;
}


void Coordinates::print (Dstr &text_out, Pad pad) const {
  if (_isNull) {
    if (pad == fixedWidth)
      text_out = "         NULL";
    else
      text_out = "NULL";
  } else {
    double latn (latitude), lngn (longitude);
    char latc, lngc;
    if (latn < 0.0) {
      latn = -latn;
      latc = 'S';
    } else
      latc = 'N';
    if (lngn < 0.0) {
      lngn = -lngn;
      lngc = 'W';
    } else
      lngc = 'E';
    char temp[80];
    if (pad == fixedWidth)
      sprintf (temp, "%7.4f° %c, %8.4f° %c", latn, latc, lngn, lngc);
    else
      sprintf (temp, "%6.4f° %c, %6.4f° %c", latn, latc, lngn, lngc);
    text_out = temp;
  }
}


void Coordinates::printLat (Dstr &text_out) const {
  if (_isNull)
    text_out = "NULL";
  else {
    double latn (latitude);
    char latc;
    if (latn < 0.0) {
      latn = -latn;
      latc = 'S';
    } else
      latc = 'N';
    char temp[80];
    sprintf (temp, "%3.1f° %c", latn, latc);
    text_out = temp;
  }
}


void Coordinates::printLng (Dstr &text_out) const {
  if (_isNull)
    text_out = "NULL";
  else {
    double lngn (longitude);
    char lngc;
    if (lngn < 0.0) {
      lngn = -lngn;
      lngc = 'W';
    } else
      lngc = 'E';
    char temp[80];
    sprintf (temp, "%3.1f° %c", lngn, lngc);
    text_out = temp;
  }
}

}
