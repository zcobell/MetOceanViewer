// $Id: xxTimestamp.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxTimestamp  Get a Timestamp from the user.  If successful, pass it
    to caller.timestamp (newTime).

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

class xxPredictionWindow;

class xxTimestamp: public xxWindow {
public:
  xxTimestamp (const xxWidget &parent,
	       xxPredictionWindow &caller,
	       Timestamp initTime,
	       const Dstr &timezone);
  ~xxTimestamp();

  void callback();

protected:
  xxPredictionWindow &_caller;
  Dstr _timezone;
  std::auto_ptr<xxWidget> goButton, cancelButton, helpLabel, spaceLabel;
  std::auto_ptr<xxTimestampDialog> dialog;
};

// Cleanup2006 Done
