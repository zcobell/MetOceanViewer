// $Id: Calendar.cc 5748 2014-10-11 19:38:53Z flaterco $

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

#include "libxtide.hh"
#include "Calendar.hh"
#include "CalendarFormC.hh"
#include "CalendarFormNotC.hh"
#include "CalendarFormH.hh"
#include "CalendarFormL.hh"
#include "CalendarFormT.hh"

namespace libxtide {


Calendar * const Calendar::factory (Station &station,
				    Timestamp startTime,
				    Timestamp endTime,
				    Mode::Mode mode,
				    Format::Format form) {
  switch (form) {
  case Format::CSV:
    return new CalendarFormC (station, startTime, endTime, mode);
  case Format::HTML:
    return new CalendarFormH (station, startTime, endTime, mode);
  case Format::LaTeX:
    return new CalendarFormL (station, startTime, endTime, mode);
  case Format::text:
    return new CalendarFormT (station, startTime, endTime, mode);
  default:
    assert (false);
  }
  return NULL; // Silence warning
}


static const bool uncorrectedOrdering (const TideEvent &a,
				       const TideEvent &b) {
  return a.uncorrectedEventTime < b.uncorrectedEventTime;
}


Calendar::Calendar (Station &station,
                    Timestamp startTime,
                    Timestamp endTime,
		    Mode::Mode mode,
		    Format::Format form):
  _mode(mode),
  _station(station),
  timezone(station.timezone),
  firstDay(startTime, timezone),
  lastDay(endTime-Interval(1), timezone) {

  // Note fencepost issue above.  End time is non-inclusive.  If
  // endTime is midnight, the real last day is one earlier.

  TideEventsOrganizer organizer;
  station.predictTideEvents (startTime, endTime, organizer);

  // Reindex the events by date (see comments in Calendar.hh).

  // For mode C or form c, events are sorted as usual.  But in the
  // other cases, where we have columns labeled max-min-max or
  // slack-flood-slack-ebb, subordinate station time warp anomalies
  // such as two high tides occurring consecutively with no
  // intervening low tide wreak havoc on the logic that matches
  // columns to tide events.

  // For maxes, mins, and slacks for which an offset was provided,
  // sanity can be restored by sorting on the original timestamps
  // instead of the corrected timestamps.  But interpolated slacks
  // have no original timestamps, and if the surrounding events from
  // which they were interpolated are anomalous, their ordering
  // relative to the other events could be indeterminate.  In the
  // presence of a time warp, SubordinateStation would be entitled to
  // find the same slack event at more than one timepoint.  (In
  // practice, it seems reluctant to find interpolated slacks in
  // anomalous cases, which is just as well.)

  // Sun and moon events and mark crossings have unique assigned
  // columns, so their sorting relative to the remaining events is
  // unimportant.  All that matters is that they remain in
  // chronological order relative to other events of the same type.

  // What matters for interpolated slacks is that the normal cases do
  // the right thing and the anomalous cases do something that isn't
  // catastrophic.

  // Consequently, the following procedure is used to reorder events
  // before their disposition into the calendar.

  // 1.  Maxes, mins, and slacks for which an offset was provided are
  // ordered by their original timestamps.

  // 2.  All other events are ordered by their regular timestamps.

  // 3.  The two lists are merged by regular timestamp, with events
  // from each list being kept in the same relative order.  The
  // merging is nonstandard since one of the lists is not necessarily
  // in sorted order.

  // There is no StrictWeakOrdering on TideEvents that would produce
  // the same result.

  // For reference stations, uncorrectedEventTime is always null, so
  // the sort and merge don't do anything.

  if (mode == Mode::altCalendar || form == Format::CSV)
    for (TideEventsIterator it = organizer.begin();
	 it != organizer.end();
	 ++it) {
      TideEvent &event (it->second);
      eventVectors[Date(event.eventTime,timezone)].push_back (event);
    }
  else {
    BetterMap<const Date, SafeVector<TideEvent> > uncorrectedSort,
                                                  correctedSort;
    for (TideEventsIterator it = organizer.begin();
	 it != organizer.end();
	 ++it) {
      TideEvent &event (it->second);
      if (event.uncorrectedEventTime.isNull())
        correctedSort[Date(event.eventTime,timezone)].push_back (event);
      else
        uncorrectedSort[Date(event.eventTime,timezone)].push_back (event);
    }

    // Sort and merge
    for (Date loopDate (firstDay); loopDate <= lastDay; ++loopDate) {
      SafeVector<TideEvent> &uncorrected (uncorrectedSort[loopDate]);
      SafeVector<TideEvent> &corrected (correctedSort[loopDate]);
      std::sort (uncorrected.begin(), uncorrected.end(), uncorrectedOrdering);
      SafeVector<TideEvent>::iterator uit (uncorrected.begin());
      SafeVector<TideEvent>::iterator cit (corrected.begin());
      while (uit != uncorrected.end() || cit != corrected.end()) {
        if (uit == uncorrected.end())
          eventVectors[loopDate].push_back (*(cit++));
        else if (cit == corrected.end())
          eventVectors[loopDate].push_back (*(uit++));
        else if (uit->eventTime < cit->eventTime)
          eventVectors[loopDate].push_back (*(uit++));
        else
          eventVectors[loopDate].push_back (*(cit++));
      }
    }
  }
}


Calendar::~Calendar() {}

}
