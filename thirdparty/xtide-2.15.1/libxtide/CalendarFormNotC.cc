// $Id: CalendarFormNotC.cc 5748 2014-10-11 19:38:53Z flaterco $

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
#include "CalendarFormNotC.hh"
#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
#include "Skycal.hh"
#endif

namespace libxtide {


CalendarFormNotC::CalendarFormNotC (Station &station,
                                    Timestamp startTime,
                                    Timestamp endTime,
				    Mode::Mode mode,
				    Format::Format form):
  Calendar (station, startTime, endTime, mode, form),
  isCurrent (station.isCurrent)
#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
  , _startTime(startTime)
#endif
 {}


void CalendarFormNotC::doRow (Dstr &text_out,
                              SafeVector<Dstr> &buf,
                              StripBool strip,
                              HeadersBool headers) {
  startRow (text_out, headers);
  for (unsigned a=0; a<buf.size(); ++a) {
    if (a)
      cellSep (text_out, headers);
    if (strip == stripOneLine) {
      Dstr strip;
      buf[a].getline (strip);
      text_out += strip;
    } else {
      text_out += buf[a];
      buf[a] = (char *)NULL;
    }
  }
  endRow (text_out, headers);
}


void CalendarFormNotC::flushBuffer (Dstr &text_out,
                                    SafeVector<Dstr> &buf,
                                    HeadersBool headers) {
  if (_mode == 'C')  // Strip off day headers
    doRow (text_out, buf, stripOneLine, isHeader);
  doRow (text_out, buf, doEntireBuffer, headers);
}


void CalendarFormNotC::print (Dstr &text_out) {
  const char pageBreakSetting (Global::settings["pb"].c);
  const char lineBreakSetting (Global::settings["lb"].c);
  text_out = (char *)NULL;
  startDoc (text_out);

  switch (_mode) {

  case Mode::altCalendar: {
    SafeVector<Dstr> weekbuf (7);
    unsigned month (UINT_MAX);
    for (Date loopDate (firstDay); loopDate <= lastDay; ++loopDate) {
      const Date::DateStruct dateStruct (loopDate.dateStruct());
      const unsigned wday (dateStruct.wday);
      if (dateStruct.mon != month) {
        if (month != UINT_MAX) {
          flushBuffer (text_out, weekbuf, notHeader);
          endTable (text_out);
          if (pageBreakSetting == 'y')
            pageBreak (text_out);
          else
            hardLineBreak (text_out);
        }
        monthBanner (text_out, loopDate);
        month = dateStruct.mon;
        startTable (text_out, 7);
      } else if (wday == 0) {
        flushBuffer (text_out, weekbuf, notHeader);
        text_out += '\n';
      }

      Dstr temp;
      loopDate.printCalendarDay (temp);
      weekbuf[wday] += temp;
      weekbuf[wday] += '\n';

      SafeVector<TideEvent> &eventVector (eventVectors[loopDate]);
      bool first (true);
      for (SafeVector<TideEvent>::iterator it (eventVector.begin());
           it != eventVector.end();
           ++it) {
        if (first)
          first = false;
        else
          hardLineBreak (weekbuf[wday]);
        TideEvent &te (*it);
        weekbuf[wday] += te.longDescription();
        if (!te.isSunMoonEvent()) {
          if (lineBreakSetting == 'y')
            hardLineBreak (weekbuf[wday]);
          else
            weekbuf[wday] += ' ';
          printPV (te.eventLevel, weekbuf[wday]);
        }
        hardLineBreak (weekbuf[wday]);
        te.eventTime.printTime (temp, timezone);
        weekbuf[wday] += temp;
      }
    }
    flushBuffer (text_out, weekbuf, notHeader);
    break;
  }


  case Mode::calendar: {
    const Dstr &eventMask (Global::settings["em"].s);
#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
    unsigned moonAge=0;
    if (eventMask.strchr('p') == -1) {
      // Initialize lunar age in a manner that will give stable results and
      // agree with XTide new moon phase outputs.
      Timestamp prevNewMoon, nextNewMoon;
      Skycal::findNewMoons (_startTime, prevNewMoon, nextNewMoon);
      assert (prevNewMoon <= _startTime);
      assert (_startTime < nextNewMoon);
      // If the next new moon happens later that same day, age is still zero.
      // Otherwise... FIXME, Date needs a difference operator.
      if (firstDay < Date(nextNewMoon,timezone))
	for (Date prevLun(prevNewMoon,timezone); prevLun<firstDay; ++prevLun)
	  ++moonAge;
      assert (moonAge < 30);
    }
#endif

    // Day ... [Mark transitions]? Surises Sunsets Moonphases
    // Tides: ... = High [Low High]+
    // Currents: ... = Slack Flood Slack [Ebb Slack Flood Slack]+
    // For Tides, X = max number of low tides in a day
    // For Currents, X = max number of max ebbs in a day
    // Exception:  ebbs with no intervening slack count as one

    // Find the value of "X" and check for mark transitions.  This is
    // a clone of the logic that is used when actually building the
    // table; X is figured based on what it tries to do.  See the
    // table-building loop for comments on the special cases.

    bool haveMarks (false);
    unsigned X;
    {
      unsigned maxtidecol (0);
      for (Date loopDate (firstDay); loopDate <= lastDay; ++loopDate) {
	unsigned tidecol (0);
	SafeVector<TideEvent> &eventVector (eventVectors[loopDate]);
	for (SafeVector<TideEvent>::iterator it (eventVector.begin());
	     it != eventVector.end();
	     ++it) {
	  TideEvent &te (*it);
	  switch (te.eventType) {
	  case TideEvent::max:
	    if (isCurrent) {
	      if (te.isMinCurrentEvent()) {
		while ((tidecol+1) % 4)
		  ++tidecol;
	      } else {
		while ((tidecol+3) % 4)
		  ++tidecol;
	      }
	    } else {
	      if (tidecol % 2)
		++tidecol;
	    }
	    break;

	  case TideEvent::min:
	    if (isCurrent) {
	      if (te.isMinCurrentEvent()) {
		while ((tidecol+3) % 4)
		  ++tidecol;
	      } else {
		while ((tidecol+1) % 4)
		  ++tidecol;
	      }
	    } else {
	      if ((tidecol+1) % 2)
		++tidecol;
	    }
	    break;

	  case TideEvent::slackrise:
	    assert (isCurrent);
            if (tidecol % 2)
	      ++tidecol;
	    break;

	  case TideEvent::slackfall:
	    assert (isCurrent);
            if (tidecol == 0)
              tidecol = 2;
            else if (tidecol % 2)
	      ++tidecol;
	    break;

	  case TideEvent::markrise:
	  case TideEvent::markfall:
	    haveMarks = true;
	  default:
	    ;
	  }
	}
	if (tidecol > maxtidecol)
	  maxtidecol = tidecol;
      }
      // maxtidecol + 1 - (3 or 1)
      if (isCurrent)
	X = std::max (1, (int) (ceil ((maxtidecol - 2) / 4.0)));
      else
	X = std::max (1, (int) (ceil (maxtidecol / 2.0)));
    }

    unsigned numtidecol (isCurrent ? 3+X*4 : 1+X*2);
    // Col. 0 is day
    // Cols. 1 .. numtidecol are tides/currents
    unsigned lastcol (numtidecol);
    // Col. numtidecol+1 is optionally mark 
    unsigned markcol (haveMarks ? ++lastcol : 0);
    // Remaining columns are as follows, or 0 if not applicable.
    unsigned p ((eventMask.strchr('p') == -1) ? ++lastcol : 0);
    unsigned S ((eventMask.strchr('S') == -1) ? ++lastcol : 0);
    unsigned s ((eventMask.strchr('s') == -1) ? ++lastcol : 0);
    unsigned M ((eventMask.strchr('M') == -1) ? ++lastcol : 0);
    unsigned m ((eventMask.strchr('m') == -1) ? ++lastcol : 0);
    // The usually blank phase column makes a natural separator between
    // the tide/current times and the sunrise/sunset times.
    SafeVector<Dstr> colbuf (lastcol+1);

    unsigned month (UINT_MAX);
    for (Date loopDate (firstDay); loopDate <= lastDay; ++loopDate) {
      const Date::DateStruct dateStruct (loopDate.dateStruct());
      if (dateStruct.mon != month) {
        if (month != UINT_MAX) {
          endTable (text_out);
          if (pageBreakSetting == 'y')
            pageBreak (text_out);
          else
            hardLineBreak (text_out);
        }
        monthBanner (text_out, loopDate);
        month = dateStruct.mon;
        startTable (text_out, lastcol+1);

        {
          unsigned i (0);
          colbuf[i++] = "Day";
          if (isCurrent) {
            colbuf[i++] += "Slack";
            startHigh (colbuf[i]);
            colbuf[i] += "Flood";
            endHigh (colbuf[i++]);
            colbuf[i++] += "Slack";
          } else {
            startHigh (colbuf[i]);
            colbuf[i] += "High";
            endHigh (colbuf[i++]);
          }
          for (unsigned a=0; a<X; ++a) {
            if (isCurrent) {
              startLow (colbuf[i]);
              colbuf[i] += "Ebb";
              endLow (colbuf[i++]);
              colbuf[i++] += "Slack";
              startHigh (colbuf[i]);
              colbuf[i] += "Flood";
              endHigh (colbuf[i++]);
              colbuf[i++] += "Slack";
            } else {
              startLow (colbuf[i]);
              colbuf[i] += "Low";
              endLow (colbuf[i++]);
              startHigh (colbuf[i]);
              colbuf[i] += "High";
              endHigh (colbuf[i++]);
            }
          }
          if (markcol) colbuf[markcol] = "Mark";
#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
          if (p) colbuf[p] = "Moon Age";
#else
          if (p) colbuf[p] = "Phase";
#endif
          if (S) colbuf[S] = "Sunrise";
          if (s) colbuf[s] = "Sunset";
          if (M) colbuf[M] = "Moonrise";
          if (m) colbuf[m] = "Moonset";
        }
        flushBuffer (text_out, colbuf, isHeader);
      }

      Dstr temp;
      loopDate.printCalendarDay (temp);
      startBold (colbuf[0]);
      colbuf[0] += temp;
      endBold (colbuf[0]);

      // Tidecol X maps to colbuf element X+1
      unsigned tidecol (0);

      SafeVector<TideEvent> &eventVector (eventVectors[loopDate]);
      for (SafeVector<TideEvent>::iterator it (eventVector.begin());
           it != eventVector.end();
           ++it) {
        TideEvent &te (*it);
        switch (te.eventType) {

        // For currents, we have the exception case of Min Floods and
        // Min Ebbs to deal with.  The combination Max, Min, Max is
        // crammed into one table cell.

        // We rely on the sorting done in Calendar::Calendar to
        // resolve all sub station time warp anomalies and leave us
        // with an ordering that goes into the table from left to
        // right.  The only problem remaining is deciding where to
        // start if the first event of the day is a slack.  Presently
        // we assume that the type of the event (rising vs. falling)
        // is authoritative, which could be risky in the presence of
        // anomalies.

        case TideEvent::max:
          if (isCurrent) {
            if (te.isMinCurrentEvent()) {
              while ((tidecol+1) % 4)
                ++tidecol;
            } else {
              while ((tidecol+3) % 4)
                ++tidecol;
            }
            assert (tidecol < numtidecol);
            if (colbuf[tidecol+1].length())
              hardLineBreak (colbuf[tidecol+1]);
          } else {
            if (tidecol % 2)
              ++tidecol;
            assert (tidecol < numtidecol);
            assert (!(colbuf[tidecol+1].length()));
          }
          te.eventTime.printTime (temp, timezone);
          colbuf[tidecol+1] += temp;
          if (lineBreakSetting == 'y')
            hardLineBreak (colbuf[tidecol+1]);
          else
            colbuf[tidecol+1] += ' ';
          printPV (te.eventLevel, colbuf[tidecol+1]);
          break;

        case TideEvent::min:
          if (isCurrent) {
            if (te.isMinCurrentEvent()) {
              while ((tidecol+3) % 4)
                ++tidecol;
            } else {
              while ((tidecol+1) % 4)
                ++tidecol;
            }
            assert (tidecol < numtidecol);
            if (colbuf[tidecol+1].length())
              hardLineBreak (colbuf[tidecol+1]);
          } else {
            if ((tidecol+1) % 2)
              ++tidecol;
            assert (tidecol < numtidecol);
            assert (!(colbuf[tidecol+1].length()));
          }
          te.eventTime.printTime (temp, timezone);
          colbuf[tidecol+1] += temp;
          if (lineBreakSetting == 'y')
            hardLineBreak (colbuf[tidecol+1]);
          else
            colbuf[tidecol+1] += ' ';
          printPV (te.eventLevel, colbuf[tidecol+1]);
          break;

#define doOtherEvent(x) \
          assert (x);                                          \
          if (colbuf[x].length())                              \
            hardLineBreak (colbuf[x]);                         \
          te.eventTime.printTime (temp, timezone);             \
          colbuf[x] += temp;                                   \
          break

        case TideEvent::slackrise:
          assert (isCurrent);
	  if (tidecol % 2)
	    ++tidecol;
          assert (tidecol < numtidecol);
          doOtherEvent(tidecol+1);

        case TideEvent::slackfall:
          assert (isCurrent);
          if (tidecol == 0)
            tidecol = 2;
	  else if (tidecol % 2)
	    ++tidecol;
          assert (tidecol < numtidecol);
          doOtherEvent(tidecol+1);

        case TideEvent::markrise:
        case TideEvent::markfall:
          doOtherEvent (markcol);

        case TideEvent::sunrise:   doOtherEvent(S);
        case TideEvent::sunset:    doOtherEvent(s);
        case TideEvent::moonrise:  doOtherEvent(M);
        case TideEvent::moonset:   doOtherEvent(m);

        case TideEvent::newmoon:
#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
	  moonAge = 0;
#endif
        case TideEvent::firstquarter:
        case TideEvent::fullmoon:
        case TideEvent::lastquarter:
#ifndef EXPERIMENTAL_MOON_AGE_NOT_PHASE
          assert (p);
          assert (!(colbuf[p].length()));
          colbuf[p] += te.longDescription();
#endif
          break;

        default:
          assert (false);
        }
      }

#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
      if (p) {
        assert (!(colbuf[p].length()));

        // This assertion fails.  If endTime falls on the day of the new
        // moon but before the actual event, we miss the reset and tack a
        // day onto the previous cycle.  This is also an unstable output
        // since it flips to 0 with a slightly later endTime.
        // assert (moonAge < 30);

        // This number wants to be replaced by graphics showing the moon as
        // it would appear (approximately) on each day of the lunar month as
        // originally suggested by Kelly Bellis, but it's not easy to
        // implement that consistently across all formats.  FWIW, Unicode
        // 1F311 through 1F318 provide characters for 8 phases of the moon
        // that could be used in UTF-8 text modes.
        colbuf[p] += moonAge++;
      }
#endif

      // Now print the day.
      flushBuffer (text_out, colbuf, notHeader);
    }
    break;
  }

  default:
    assert (false);
  }

  // The above leave us with an unfinished table.
  endTable (text_out);
  endDoc (text_out);
}


void CalendarFormNotC::printPV (const PredictionValue &pv, Dstr &buf) {
  Dstr temp;
  pv.printnp (temp);
  buf += temp;
}


void CalendarFormNotC::monthBanner   (Dstr &buf unusedParameter,
                                      Date date unusedParameter) {}
void CalendarFormNotC::hardLineBreak (Dstr &buf unusedParameter) {}
void CalendarFormNotC::startHigh     (Dstr &buf unusedParameter) {}
void CalendarFormNotC::endHigh       (Dstr &buf unusedParameter) {}
void CalendarFormNotC::startLow      (Dstr &buf unusedParameter) {}
void CalendarFormNotC::endLow        (Dstr &buf unusedParameter) {}
void CalendarFormNotC::startDoc      (Dstr &buf unusedParameter) {}
void CalendarFormNotC::endDoc        (Dstr &buf unusedParameter) {}
void CalendarFormNotC::pageBreak     (Dstr &buf unusedParameter) {}
void CalendarFormNotC::startTable    (Dstr &buf unusedParameter,
                                      unsigned numcols unusedParameter) {}
void CalendarFormNotC::endTable      (Dstr &buf unusedParameter) {}
void CalendarFormNotC::startRow      (Dstr &buf unusedParameter,
                                      HeadersBool headers unusedParameter) {}
void CalendarFormNotC::endRow        (Dstr &buf unusedParameter,
                                      HeadersBool headers unusedParameter) {}
void CalendarFormNotC::cellSep       (Dstr &buf unusedParameter,
                                      HeadersBool headers unusedParameter) {}
void CalendarFormNotC::startBold     (Dstr &buf unusedParameter) {}
void CalendarFormNotC::endBold       (Dstr &buf unusedParameter) {}

}
