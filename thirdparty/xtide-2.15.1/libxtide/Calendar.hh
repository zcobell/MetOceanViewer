// $Id: Calendar.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  Calendar  Manage construction, organization, and printing of calendars.

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

class Calendar {
public:

  static Calendar * const factory (Station &station,
				   Timestamp startTime,
				   Timestamp endTime,
				   Mode::Mode mode,
				   Format::Format form);

  virtual ~Calendar();
  virtual void print (Dstr &text_out) = 0;

protected:
  const Mode::Mode _mode;

  /*

  Traversing a TideEventsOrganizer using Timestamp::nextDay to locate
  day boundaries works 99.999% of the time, give or take a 9, but it's
  still wrong.  The assumption that the date will increase
  monotonically when expressed in local time is false.

  For example, the following timestamps are in chronological order for
  the time zone :America/St_Johns:

    1990-10-27 23:59 NDT
    1990-10-28 00:00 NDT
    1990-10-27 23:01 NST
            ...
    1990-10-27 23:59 NST
    1990-10-28 00:00 NST

  It's October 28th for one minute and then it's October 27th again.
  The clock is set back one hour not at midnight, but at one minute
  past midnight.  From tzdata2007a:

  # Newfoundland and Labrador
  # Rule  NAME    FROM    TO      TYPE    IN      ON      AT      SAVE
  Rule    StJohns 1987    only    -       Apr     Sun>=1  0:01    1:00    D
  Rule    StJohns 1987    2006    -       Oct     lastSun 0:01    0       S
  Rule    StJohns 1988    only    -       Apr     Sun>=1  0:01    2:00    DD
  Rule    StJohns 1989    2006    -       Apr     Sun>=1  0:01    1:00    D

  The correct model is to index by day instead of by Timestamp, then
  sort by Timestamp within each day.

  For additional complications see the commentary in Calendar.cc.

  */

  BetterMap<const Date, SafeVector<TideEvent> > eventVectors;

  Station &_station;
  const Dstr timezone;
  Date firstDay, lastDay;


  Calendar (Station &station,
	    Timestamp startTime,
	    Timestamp endTime,
            Mode::Mode mode,
	    Format::Format form);

private:
  // Prohibited operations not implemented.
  Calendar (const Calendar &);
  Calendar &operator= (const Calendar &);
};

}
