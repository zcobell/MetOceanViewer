// $Id: TideEvent.cc 5748 2014-10-11 19:38:53Z flaterco $

// TideEvent  Generic representation for tide events.

/*
    Copyright (C) 2004  David Flater.

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

// For VC++, getpid is in process.h.
#ifdef HAVE_PROCESS_H
#include <process.h>
#endif

namespace libxtide {


const bool TideEvent::isSunMoonEvent () const {
  switch (eventType) {
  case TideEvent::sunrise:
  case TideEvent::sunset:
  case TideEvent::moonrise:
  case TideEvent::moonset:
  case TideEvent::newmoon:
  case TideEvent::firstquarter:
  case TideEvent::fullmoon:
  case TideEvent::lastquarter:
    return true;
  default:
    return false;
  }
}


const bool TideEvent::isMaxMinEvent () const {
  switch (eventType) {
  case TideEvent::max:
  case TideEvent::min:
    return true;
  default:
    return false;
  }
}


const bool TideEvent::isMinCurrentEvent () const {
  switch (eventType) {
  case TideEvent::max:
    return (isCurrent && (eventLevel.val() < 0.0));
  case TideEvent::min:
    return (isCurrent && (eventLevel.val() > 0.0));
  default:
    return false;
  }
}


constString TideEvent::longDescription () const {
  switch (eventType) {
  case TideEvent::max:
    return (isCurrent ? (eventLevel.val() >= 0.0 ? "Max Flood"
			       : "Min Ebb") : "High Tide");
  case TideEvent::min:
    return (isCurrent ? (eventLevel.val() <= 0.0 ? "Max Ebb"
                               : "Min Flood") : "Low Tide");
  case TideEvent::slackrise:
    return "Slack, Flood Begins";
  case TideEvent::slackfall:
    return "Slack, Ebb Begins";
  case TideEvent::markrise:
    if (isCurrent) {
      if (eventLevel.val() < 0.0)
        return "Mark, Ebb Decreasing";
      if (eventLevel.val() > 0.0)
        return "Mark, Flood Increasing";
      return "Mark, Flood Begins";
    }
    return "Mark Rising";
  case TideEvent::markfall:
    if (isCurrent) {
      if (eventLevel.val() < 0.0)
        return "Mark, Ebb Increasing";
      if (eventLevel.val() > 0.0)
        return "Mark, Flood Decreasing";
      return "Mark, Ebb Begins";
    }
    return "Mark Falling";
  case TideEvent::sunrise:
    return "Sunrise";
  case TideEvent::sunset:
    return "Sunset";
  case TideEvent::moonrise:
    return "Moonrise";
  case TideEvent::moonset:
    return "Moonset";
  case TideEvent::newmoon:
    return "New Moon";
  case TideEvent::firstquarter:
    return "First Quarter";
  case TideEvent::fullmoon:
    return "Full Moon";
  case TideEvent::lastquarter:
    return "Last Quarter";
  case TideEvent::rawreading:
  default:
    assert (false);
  }
  return NULL; // Silence bogus "control reaches end of non-void function"
}


// This is only used for the bottom blurb line in graphs.
// Max length 5 characters.
constString TideEvent::shortDescription () const {
  switch (eventType) {
  case TideEvent::slackrise:
  case TideEvent::slackfall:
    return "Slack";
  case TideEvent::markrise:
  case TideEvent::markfall:
    return "Mark";
  case TideEvent::moonrise:
    return "Mrise";
  case TideEvent::moonset:
    return "Mset";
  case TideEvent::newmoon:
    return "New";
  case TideEvent::firstquarter:
    return "1st/4";
  case TideEvent::fullmoon:
    return "Full";
  case TideEvent::lastquarter:
    return "3rd/4";
  default:
    assert (false);
  }
  return NULL; // Silence bogus "control reaches end of non-void function"
}


void TideEvent::print (Dstr &text_out,
                       Mode::Mode mode,
                       Format::Format form,
		       const Station &station) const {
  Dstr timePrint, levelPrint;

  switch (mode) {
  case Mode::raw:
    switch (form) {
    case Format::CSV:
      text_out = station.name;
      text_out.repchar (',', Global::CSV_repchar);
      text_out += ',';
      text_out += eventTime.timet();
      text_out += ',';
      text_out += eventLevel.val();
      return;
    case Format::text:
      text_out = eventTime.timet();
      text_out += ' ';
      text_out += eventLevel.val();
      return;
    default:
      assert (false);
    }

  case Mode::mediumRare:
    switch (form) {
    case Format::CSV:
      text_out = station.name;
      text_out.repchar (',', Global::CSV_repchar);
      text_out += ',';
      eventTime.printDate (timePrint, station.timezone);
      text_out += timePrint;
      text_out += ',';
      eventTime.printTime (timePrint, station.timezone);
      text_out += timePrint;
      text_out += ',';
      text_out += eventLevel.val();
      return;
    case Format::text:
      eventTime.print (text_out, station.timezone);
      text_out += ' ';
      text_out += eventLevel.val();
      return;
    default:
      assert (false);
    }

  case Mode::plain:
    switch (form) {
    case Format::text:
      if (!isSunMoonEvent())
	eventLevel.print (levelPrint);
      eventTime.print (timePrint, station.timezone);
      text_out = timePrint;
      text_out += ' ';
      text_out += levelPrint;
      text_out += "  ";
      text_out += longDescription ();
      return;

    case Format::CSV:
      if (!isSunMoonEvent())
	eventLevel.printnp (levelPrint);
      text_out = station.name;
      text_out.repchar (',', Global::CSV_repchar);
      text_out += ',';
      eventTime.printDate (timePrint, station.timezone);
      text_out += timePrint;
      text_out += ',';
      eventTime.printTime (timePrint, station.timezone);
      text_out += timePrint;
      text_out += ',';
      text_out += levelPrint;
      text_out += ',';
      {
        Dstr mangle (longDescription());
        mangle.repchar (',', Global::CSV_repchar);
        text_out += mangle;
      }
      return;

    case Format::iCalendar:
      {
	if (!isSunMoonEvent())
	  eventLevel.print (levelPrint);

	static unsigned long snum = 0;
	char temp[80];

	// Fields required by RFC 2446 for PUBLISH method on VEVENT:
	//   DTSTAMP
	//   DTSTART
	//   ORGANIZER
	//   SUMMARY (can be null)
	//   UID
	// Optional fields of interest:
	//   COMMENT
	//   DESCRIPTION
	//   GEO
	//   LOCATION
	//   TRANSP

	// RFC 2445 p. 52:
	//
	// For cases where a "VEVENT" calendar component specifies a
	// "DTSTART" property with a DATE-TIME data type but no "DTEND"
	// property, the event ends on the same calendar date and time of
	// day specified by the "DTSTART" property.

	// TRANSP should be redundant for an event that takes up no time,
	// but being explicit may help less enlightened clients to do the
	// right thing.

	Timestamp now ((time_t)(time(NULL)));
        Dstr nowPrint;
	now.print_iCalendar (nowPrint, Timestamp::keepSecs);

	text_out = "BEGIN:VEVENT\r\nDTSTAMP:";
	text_out += nowPrint;
	text_out += "\r\nDTSTART:";
	eventTime.print_iCalendar (timePrint, Timestamp::zeroOutSecs);
	text_out += timePrint;
	text_out += "\r\nTRANSP:TRANSPARENT\r\nORGANIZER;CN=XTide:MAILTO:nobody@localhost\r\nSUMMARY:";
	text_out += longDescription();
	if (!levelPrint.isNull()) {
	  text_out += ' ';
	  text_out += levelPrint;
	}
	text_out += "\r\nUID:XTide-";
	if (!station.coordinates.isNull()) {
	  sprintf (temp, "%ld-%ld-", (long)(station.coordinates.lat()*10000),
		   (long)(station.coordinates.lng()*10000));
	  text_out += temp;
	}
	eventTime.print_iCalendar (timePrint, Timestamp::keepSecs);
	text_out += timePrint;

	// Latitude + longitude + event time should be nearly unique, but
	// "you can never have too much overkill."

	text_out += "-\r\n ";
	text_out += nowPrint;
	text_out += '-';
	text_out += getpid();
	text_out += '-';
	text_out += ++snum;
	text_out += '-';
	text_out += rand();
	text_out += "@localhost\r\n";
	if (!station.coordinates.isNull()) {
	  sprintf (temp, "GEO:%6.4f;%6.4f\r\n", station.coordinates.lat(),
		                                station.coordinates.lng());
	  text_out += temp;
	}
	text_out += "LOCATION:";
	{
	  Dstr mangle (station.name);
	  mangle.rfc2445_mangle();
	  text_out += mangle;
	}
	text_out += "\r\nEND:VEVENT\r";
      }
      return;

    default:
      assert (false);
    }

  default:
    assert (false);
  }
}

}
