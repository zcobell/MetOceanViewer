// $Id: CalendarFormH.hh 5748 2014-10-11 19:38:53Z flaterco $

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

class CalendarFormH: public CalendarFormNotC {
public:
  CalendarFormH (Station &station,
		 Timestamp startTime,
		 Timestamp endTime,
		 Mode::Mode mode);

protected:
  // Change hyphens to minuses.
  void printPV (const PredictionValue &pv, Dstr &buf);

  void monthBanner   (Dstr &buf, Date date);
  void hardLineBreak (Dstr &buf);
  void pageBreak     (Dstr &buf);
  void endHigh       (Dstr &buf);
  void startLow      (Dstr &buf);
  void startDoc      (Dstr &buf);
  void endDoc        (Dstr &buf);
  void startTable    (Dstr &buf, unsigned numcols);
  void endTable      (Dstr &buf);
  void startRow      (Dstr &buf, HeadersBool headers);
  void endRow        (Dstr &buf, HeadersBool headers);
  void cellSep       (Dstr &buf, HeadersBool headers);
  void startBold     (Dstr &buf);
  void endBold       (Dstr &buf);
};

}
