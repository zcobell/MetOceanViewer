// $Id: CalendarFormT.cc 5748 2014-10-11 19:38:53Z flaterco $

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
#include "CalendarFormT.hh"

namespace libxtide {


CalendarFormT::CalendarFormT (Station &station,
                              Timestamp startTime,
			      Timestamp endTime,
			      Mode::Mode mode):
  CalendarFormNotC (station, startTime, endTime, mode, Format::text) {}


// Returns true if buf[0] through buf[buflen-1] are all empty or null.
static const bool isNullLine (SafeVector<Dstr> &buf) {
  for (unsigned a=0; a<buf.size(); ++a)
    if (buf[a].length())
      return false;
  return true;
}


void CalendarFormT::flushBuffer (Dstr &text_out,
                                 SafeVector<Dstr> &buf,
                                 HeadersBool headers unusedParameter) {
  char fmt[80];
  unsigned columnWidth (Global::settings["tw"].u / buf.size());
  if (columnWidth < 2)
    return;
  SafeVector<char> tbuf (columnWidth+1);
  sprintf (fmt, "%%-%u.%us ", columnWidth-1, columnWidth-1);

  while (!isNullLine (buf)) {
    for (unsigned a=0; a<buf.size(); ++a) {
      Dstr strip;
      buf[a].getline (strip);
      const int numspaces (((int)columnWidth - (int)strip.length()) / 2);
      for (int a=0; a<numspaces; ++a)
        strip *= ' ';
      sprintf (&(tbuf[0]), fmt, strip.aschar());
      text_out += &(tbuf[0]);
    }
    text_out += '\n';
  }
}


void CalendarFormT::monthBanner (Dstr &buf, Date date) {
  Dstr heading;
  date.printCalendarHeading (heading);
  const int numspaces (((int)(Global::settings["tw"].u) -
			(int)(heading.length())) / 2);
  for (int a=0; a<numspaces; ++a)
    buf += ' ';
  buf += heading;
  buf += '\n';
}


void CalendarFormT::hardLineBreak (Dstr &buf) {
  buf += '\n';
}


void CalendarFormT::pageBreak (Dstr &buf) {
  buf += '\f';
  Dstr temp;
  _station.textBoilerplate (temp, Format::text, false);
  buf += temp;
}


void CalendarFormT::startTable (Dstr &buf,
                                unsigned numcols unusedParameter) {
  buf += '\n';
}

}
