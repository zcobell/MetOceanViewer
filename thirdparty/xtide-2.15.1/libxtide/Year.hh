// $Id: Year.hh 5748 2014-10-11 19:38:53Z flaterco $

/*
    Copyright (C) 1997  David Flater.

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

class Year {
public:
  Year (uint16_t year);  // Acceptable range is 1 to 4000.
  const uint16_t val() const;

  // Prefix increment
  const Year operator++ ();

protected:
  uint16_t _year;
};

const Year operator +  (Year y, int n);
const Year operator -  (Year y, int n);
const bool operator <  (Year y1, Year y2);
const bool operator >  (Year y1, Year y2);
const bool operator >= (Year y1, Year y2);
const bool operator <= (Year y1, Year y2);
const bool operator == (Year y1, Year y2);
const bool operator != (Year y1, Year y2);

}
