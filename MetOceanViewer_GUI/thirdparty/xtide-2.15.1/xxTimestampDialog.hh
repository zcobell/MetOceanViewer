// $Id: xxTimestampDialog.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxTimestampDialog  Embeddable timestamp chooser.

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

class xxTimestampDialog {
public:
  xxTimestampDialog (const xxWidget &parent,
		     constString caption,
		     Timestamp initTime,
		     const Dstr &timezone);

  // Returns an ISO 8601 string.  It's not a Timestamp because then you
  // would just get a null timestamp in case of error.
  void val (Dstr &ISO_string_out);

protected:
  const Dstr _timezone;
  std::auto_ptr<xxWidget> box, label, spaceLabel1, spaceLabel2;
  std::auto_ptr<xxMultiChoice> yearChoice, monthChoice, dayChoice, hourChoice,
                               minuteChoice;

private:
  // Prohibited operations not implemented.
  xxTimestampDialog (const xxTimestampDialog &);
  xxTimestampDialog &operator= (const xxTimestampDialog &);
};

// Cleanup2006 Done
