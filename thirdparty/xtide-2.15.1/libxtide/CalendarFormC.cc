// $Id: CalendarFormC.cc 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {


// Controls for how many columns you get in calendar mode, CSV format.
// If you get more events than there is room for, they are discarded.

// A value of x for numMaxMin means that you get x columns for the max
// times, x columns for the max heights, x columns for the min times,
// x columns for the min heights, and 2x columns for slacks.
static const unsigned numMaxMin (5U);

// A value of x for numRiseSet means that you get x columns for
// sunrise, x columns for sunset, x columns for moonrise, and x
// columns for moonset.
//
// Yes, you can have two sunsets in one day, and you don't even need
// Daylight Savings Time to do it:
//
// Isla Neny, Antarctica
// 68.2000° S, 67.0000° W
//
// 2001-01-24 12:03 AM ARST   Sunset
// 2001-01-24  3:17 AM ARST   Sunrise
// 2001-01-24 11:57 PM ARST   Sunset
static const unsigned numRiseSet (1U);

// Moon phases and mark crossings are discarded.


CalendarFormC::CalendarFormC (Station &station,
                              Timestamp startTime,
			      Timestamp endTime,
			      Mode::Mode mode):
  Calendar (station, startTime, endTime, mode, Format::CSV) {}


static void addCSVevent (SafeVector<TideEvent> &events,
			 unsigned limit,
			 const TideEvent &tideEvent,
			 const Dstr &date,
			 constString desc) {

  // The limits on number of events are necessary for the CSV format
  // to be consistent.

  if (events.size() == limit) {
    Dstr details ("Warning:  too many ");
    details += desc;
    details += " events on ";
    details += date;
    details += "; increase ";
    if (tideEvent.isSunMoonEvent())
      details += "numRiseSet";
    else
      details += "numMaxMin";
    details += " in CalendarFormC.cc";
    Global::log (details, LOG_WARNING);
  } else
    events.push_back (tideEvent);
}


static void printCSVmaxmin (Dstr &text_out,
			    const SafeVector<TideEvent> &events,
			    unsigned limit,
                            const Dstr &timezone) {
  Dstr t;
  for (unsigned i=0; i<limit; ++i) {
    text_out += ',';
    if (i < events.size()) {
      events[i].eventTime.printTime (t, timezone);
      text_out += t;
      text_out += ',';
      events[i].eventLevel.printnp (t);
      text_out += t;
    } else
      text_out += ',';
  }
}


static void printCSVother (Dstr &text_out,
			   const SafeVector<TideEvent> &events,
			   unsigned limit,
                           const Dstr &timezone) {
  Dstr t;
  for (unsigned i=0; i<limit; ++i) {
    text_out += ',';
    if (i < events.size()) {
      events[i].eventTime.printTime (t, timezone);
      text_out += t;
    }
  }
}


void CalendarFormC::print (Dstr &text_out) {
  assert (_mode == Mode::calendar);
  text_out = (char *)NULL;

  Dstr stationName (_station.name);
  stationName.repchar (',', Global::CSV_repchar);

  for (Date loopDate (firstDay); loopDate <= lastDay; ++loopDate) {
    Dstr date;
    loopDate.print (date);
    SafeVector<TideEvent> maxes,
                          mins,
                          slacks,
                          sunrises,
                          sunsets,
                          moonrises,
                          moonsets;

    SafeVector<TideEvent> &eventVector (eventVectors[loopDate]);
    for (SafeVector<TideEvent>::iterator it (eventVector.begin());
         it != eventVector.end();
         ++it) {
      TideEvent &te (*it);
      switch (te.eventType) {
      case TideEvent::max:
	addCSVevent (maxes, numMaxMin, te, date, "max");
	break;
      case TideEvent::min:
	addCSVevent (mins, numMaxMin, te, date, "min");
	break;
      case TideEvent::slackrise:
      case TideEvent::slackfall:
	addCSVevent (slacks, 2*numMaxMin, te, date, "slack");
	break;
      case TideEvent::sunrise:
	addCSVevent (sunrises, numRiseSet, te, date, "sunrise");
	break;
      case TideEvent::sunset:
	addCSVevent (sunsets, numRiseSet, te, date, "sunset");
	break;
      case TideEvent::moonrise:
	addCSVevent (moonrises, numRiseSet, te, date, "moonrise");
	break;
      case TideEvent::moonset:
	addCSVevent (moonsets, numRiseSet, te, date, "moonset");
	break;
      default:
	;
      }
    }
    text_out += stationName;
    text_out += ',';
    text_out += date;
    printCSVmaxmin (text_out, maxes,     numMaxMin,  timezone);
    printCSVmaxmin (text_out, mins,      numMaxMin,  timezone);
    printCSVother  (text_out, slacks,  2*numMaxMin,  timezone);
    printCSVother  (text_out, sunrises,  numRiseSet, timezone);
    printCSVother  (text_out, sunsets,   numRiseSet, timezone);
    printCSVother  (text_out, moonrises, numRiseSet, timezone);
    printCSVother  (text_out, moonsets,  numRiseSet, timezone);
    text_out += '\n';
  }
}

}
