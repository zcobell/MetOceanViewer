// $Id: Year.cc 5748 2014-10-11 19:38:53Z flaterco $

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

#include "libxtide.hh"
namespace libxtide {


Year::Year (uint16_t year):
  _year(year) {

  // If they tried to specify a negative year it would show up as a
  // big positive number, so don't confuse them with details.

  if (year < 1 || year > 4000)
    Global::barf (Error::YEAR_OUT_OF_RANGE);
}


const uint16_t Year::val() const {
  return _year;
}


const Year operator + (Year y, int n) {
  return Year (y.val() + n);
}


const Year operator - (Year y, int n) {
  return Year (y.val() - n);
}


const bool operator < (Year y1, Year y2) {
  return (y1.val() < y2.val());
}


const bool operator > (Year y1, Year y2) {
  return (y1.val() > y2.val());
}


const bool operator >= (Year y1, Year y2) {
  return (y1.val() >= y2.val());
}


const bool operator <= (Year y1, Year y2) {
  return (y1.val() <= y2.val());
}


const bool operator == (Year y1, Year y2) {
  return (y1.val() == y2.val());
}


const bool operator != (Year y1, Year y2) {
  return (y1.val() != y2.val());
}


// Prefix increment
const Year Year::operator++ () {
  if (++_year > 4000)
    Global::barf (Error::YEAR_OUT_OF_RANGE);
  return *this;
}

}
