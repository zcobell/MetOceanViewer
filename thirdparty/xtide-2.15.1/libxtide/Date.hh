// $Id: Date.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {

class Date {
public:

  Date (Timestamp timestamp, const Dstr &timezone);

  void print (Dstr &text_out) const;               // See Timestamp::printDate.
  void printCalendarDay (Dstr &text_out) const;    // See Timestamp.
  void printCalendarHeading (Dstr &text_out) const;// See Timestamp.
  const Date operator++ ();                        // Prefix increment.

  // struct tm 'lite'
  struct DateStruct {
    unsigned wday; // Number of days since Sunday, [0, 6].
    unsigned mon;  // Number of months since January, [0, 11].
  };

  // Accessors.
  const DateStruct dateStruct() const;
  const date_t date() const;

protected:
  Dstr timezoneLocalVar;
  Timestamp _timestamp;
};

const bool operator<  (const Date &a, const Date &b);
const bool operator<= (const Date &a, const Date &b);

}
