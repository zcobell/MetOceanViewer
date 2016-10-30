// $Id: CalendarFormH.cc 5748 2014-10-11 19:38:53Z flaterco $

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
#include "CalendarFormH.hh"

namespace libxtide {


CalendarFormH::CalendarFormH (Station &station,
                              Timestamp startTime,
                              Timestamp endTime,
			      Mode::Mode mode):
  CalendarFormNotC (station, startTime, endTime, mode, Format::HTML) {}


void CalendarFormH::monthBanner (Dstr &buf, Date date) {
  Dstr heading;
  date.printCalendarHeading (heading);
  buf += "<h2>";
  buf += heading;
  buf += "</h2>\n";
}


void CalendarFormH::printPV (const PredictionValue &pv, Dstr &buf) {
  Dstr temp;
  pv.printnp (temp);
  temp.repstr ("-", "&minus;");
  buf += temp;
}


void CalendarFormH::hardLineBreak (Dstr &buf) {
  buf += "<br>\n";
}


void CalendarFormH::pageBreak (Dstr &buf) {
  buf += "</center>\n\n";
  Dstr temp;
  _station.textBoilerplate (temp, Format::HTML, false);
  buf += temp;
  buf += "<center>\n";
}


void CalendarFormH::startDoc (Dstr &buf) {
  buf += "<center>\n";
}


void CalendarFormH::endDoc (Dstr &buf) {
  buf += "</center>\n";
}


void CalendarFormH::startTable (Dstr &buf,
                                unsigned numcols unusedParameter) {
  buf += "<table border>\n";
}


void CalendarFormH::endTable (Dstr &buf) {
  buf += "</table>\n";
}


void CalendarFormH::startRow (Dstr &buf,
                              HeadersBool headers) {
  buf += (headers == isHeader ? "<tr><th><small>" : "<tr><td align=\"center\"><small>");
}


void CalendarFormH::endRow (Dstr &buf, HeadersBool headers) {
  buf += (headers == isHeader ? "</small></th></tr>\n"
                              : "</small></td></tr>\n");
}


void CalendarFormH::cellSep (Dstr &buf, HeadersBool headers) {
  buf += (headers == isHeader ? "</small></th><th><small>"
                              : "</small></td><td align=\"center\"><small>");
}


void CalendarFormH::startBold (Dstr &buf) {
  buf += "<b>";
}


void CalendarFormH::endBold (Dstr &buf) {
  buf += "</b>";
}


void CalendarFormH::endHigh (Dstr &buf) {
  buf += "<br><br>"; // It takes two.
}


void CalendarFormH::startLow (Dstr &buf) {
  buf += "<br>";
}

}
