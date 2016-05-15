// $Id: CalendarFormNotC.hh 5748 2014-10-11 19:38:53Z flaterco $

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

class CalendarFormNotC: public Calendar {
public:
  CalendarFormNotC (Station &station,
                    Timestamp startTime,
                    Timestamp endTime,
		    Mode::Mode mode,
		    Format::Format form);

  void print (Dstr &text_out);

protected:
  const bool isCurrent;
#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
  const Timestamp _startTime;
#endif

  enum HeadersBool {isHeader, notHeader};
  enum StripBool {stripOneLine, doEntireBuffer};

  void doRow (Dstr &text_out,
              SafeVector<Dstr> &buf,
              StripBool strip,
              HeadersBool headers);

  // Wrapper for doRow to strip day headers in C mode.
  // Overridden by text.
  virtual void flushBuffer (Dstr &text_out,
                            SafeVector<Dstr> &buf,
                            HeadersBool headers);

  // Append pv.printnp() to buf.
  // Overridden by LaTeX and HTML.
  virtual void printPV (const PredictionValue &pv, Dstr &buf);

  // Append to buf the correct markup for the various formats.  At
  // present, Calendar is the only place where it makes sense to have
  // this; otherwise, these functions should be broken out and put
  // somewhere else.
  virtual void monthBanner   (Dstr &buf, Date date);
  virtual void hardLineBreak (Dstr &buf);
  virtual void startHigh     (Dstr &buf);
  virtual void endHigh       (Dstr &buf);
  virtual void startLow      (Dstr &buf);
  virtual void endLow        (Dstr &buf);
  virtual void startDoc      (Dstr &buf);
  virtual void endDoc        (Dstr &buf);
  virtual void pageBreak     (Dstr &buf);
  virtual void startTable    (Dstr &buf, unsigned numcols);
  virtual void endTable      (Dstr &buf);
  virtual void startRow      (Dstr &buf, HeadersBool headers);
  virtual void endRow        (Dstr &buf, HeadersBool headers);
  virtual void cellSep       (Dstr &buf, HeadersBool headers);
  virtual void startBold     (Dstr &buf);
  virtual void endBold       (Dstr &buf);
};

}
