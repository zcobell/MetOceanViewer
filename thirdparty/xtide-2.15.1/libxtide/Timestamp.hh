// $Id: Timestamp.hh 5748 2014-10-11 19:38:53Z flaterco $

// Timestamp:  A point in time.  See also Year, Date, Interval.

/*
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

namespace libxtide {

class Timestamp: public Nullable {
public:

  // Create a null Timestamp.
  Timestamp ();

  // Create a Timestamp from a Posix timestamp.
  Timestamp (time_t posixTime);

  // Create a Timestamp for the beginning of the specified year in UTC
  // (YEAR-01-01 00:00:00Z), or a null timestamp if not possible.
  Timestamp (Year year);

  // Create a Timestamp corresponding to a string in YYYY-MM-DD HH:MM
  // format, but with separate time zone.  Return null on error.
  // WARNING:  SECONDS, IF SPECIFIED, ARE TRUNCATED TO THE PREVIOUS MINUTE.
  Timestamp (const Dstr &timeString, const Dstr &timezone);

  // Create a Timestamp for the specified Julian date.
  Timestamp (double julianDate);

  // Convert to Julian date.  Error if null.
  const double jd() const;

  // Convert to time_t.  Error if null.
  const time_t timet() const;

  // Convert to tm in specified time zone.
  const tm tmStruct (const Dstr &timezone) const;

  // Return the UTC year that this timestamp falls in.  Error if null.
  const Year year() const;

  // Return the day that this timestamp falls in.  Error if null.
  const date_t date (const Dstr &timezone) const;

  // Output timestamp in local time zone, no seconds.  Error if null.
  void print                (Dstr &text_out, const Dstr &timezone) const;
  void printHour            (Dstr &text_out, const Dstr &timezone) const;
  void printDate            (Dstr &text_out, const Dstr &timezone) const;
  void printTime            (Dstr &text_out, const Dstr &timezone) const;
  void printCalendarHeading (Dstr &text_out, const Dstr &timezone) const;
  void printCalendarDay     (Dstr &text_out, const Dstr &timezone) const;

  // Output timestamp in format complying with RFC 2445 (iCalendar).
  // zeroOutSecs:  YYYYMMDDTHHMM00Z
  // keepSecs:     YYYYMMDDTHHMMSSZ
  enum SecStyle {zeroOutSecs, keepSecs};
  void print_iCalendar (Dstr &text_out, SecStyle secStyle) const;

  // Can't do this when null.
  void operator+= (Interval b);
  void operator-= (Interval b);

  // This forces initialization of the time zone mapping and returns
  // true if it can do Rarotonga correctly.
  const bool zoneinfoIsNotHorriblyObsolete() const;

  // Following are four methods for finding hour transitions (i.e.,
  // minutes and seconds are 0) and day transitions (i.e., midnight).
  // This is not as simple as one might think.

  // If an hour transition does not exist due to a DST change,
  // floorHour and nextHour will go on to the following hour
  // transition.  If a day transition does not exist due to a DST
  // change, floorDay and nextDay will return the time of the
  // discontinuity.  If a day transition occurs more than once due to
  // a DST change (see example in Calendar.hh), it is not
  // deterministic which of the timepoints floorDay and nextDay will
  // return.  However, it is assured that the timepoint returned by
  // floorDay will be in the same day as where you started, and it is
  // assured that the timepoint returned by nextDay will be in the
  // next day.  In the pathological case where midnight comes twice,
  // nextDay will not jump from one twin to the other but will always
  // go to the next day.  This causes one of the midnights not to be
  // marked in graphs but is necessary for nextDay to be usable as an
  // incrementor function in Date.

  // Find the previous hour or day transition given an arbitrary
  // starting time.  Like the mathematical floor operation, these
  // return the input unchanged if it is already on the boundary.
  void floorHour (const Dstr &timezone);   // Error if timestamp is null.
  void floorDay  (const Dstr &timezone);   // Error if timestamp is null.

  // Find the next hour or day transition assuming that we are already
  // on the previous transition.
  void nextHour  (const Dstr &timezone);   // Error if timestamp is null.
  void nextDay   (const Dstr &timezone);   // Error if timestamp is null.

protected:

  time_t _posixTime;

  // strftime in specified timezone with format fmt
  void strftime (Dstr &text_out,
                 const Dstr &timezone,
                 const Dstr &formatString) const;
};

// Comparing with a null is an error.
const bool operator>  (Timestamp a, Timestamp b);
const bool operator>= (Timestamp a, Timestamp b);
const bool operator<  (Timestamp a, Timestamp b);
const bool operator<= (Timestamp a, Timestamp b);
const bool operator== (Timestamp a, Timestamp b);
const bool operator!= (Timestamp a, Timestamp b);

// You can't add or subtract from a null.
const Timestamp operator+ (Timestamp a, Interval b);
const Timestamp operator- (Timestamp a, Interval b);
const Interval  operator- (Timestamp a, Timestamp b);

}
