// $Id: Date.cc 5748 2014-10-11 19:38:53Z flaterco $

// Date:  A particular day.  See also Year, Timestamp, Interval.

/*
    Copyright (C) 2007  David Flater.

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


Date::Date (Timestamp timestamp, const Dstr &timezone):
  timezoneLocalVar(timezone),
  _timestamp(timestamp) {
  _timestamp.floorDay (timezoneLocalVar);
}


// Prefix increment
const Date Date::operator++ () {
  _timestamp.nextDay (timezoneLocalVar);
  return *this;
}


const date_t Date::date() const {
  return _timestamp.date (timezoneLocalVar);
}


const Date::DateStruct Date::dateStruct() const {
  tm tempTm (_timestamp.tmStruct(timezoneLocalVar));
  assert (tempTm.tm_wday >= 0 && tempTm.tm_mon >= 0 &&
          tempTm.tm_wday < 7 && tempTm.tm_mon < 12);
  DateStruct tempDateStruct = {(unsigned)tempTm.tm_wday,
			       (unsigned)tempTm.tm_mon};
  return tempDateStruct;
}


void Date::print (Dstr &text_out) const {
  _timestamp.printDate (text_out, timezoneLocalVar);
}


void Date::printCalendarDay (Dstr &text_out) const {
  _timestamp.printCalendarDay (text_out, timezoneLocalVar);
}


void Date::printCalendarHeading (Dstr &text_out) const {
  _timestamp.printCalendarHeading (text_out, timezoneLocalVar);
}


// Simply comparing the timestamps does not work because the timestamp
// pertaining to the beginning of a given day is not necessarily
// unique.  See discussion in Timestamp.hh for floorDay and in
// Calendar.hh.
const bool operator< (const Date &a, const Date &b) {
  return (a.date() < b.date());
}


const bool operator<= (const Date &a, const Date &b) {
  return (a.date() <= b.date());
}

}
