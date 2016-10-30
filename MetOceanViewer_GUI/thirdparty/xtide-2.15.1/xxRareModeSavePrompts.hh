// $Id: xxRareModeSavePrompts.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxRareModeSavePrompts  Get a file name and two timestamps from the
    user.  If successful, do caller.save (filename, startTime, endTime).

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

class xxTextMode;

class xxRareModeSavePrompts: public xxWindow {
public:
  xxRareModeSavePrompts (const xxWidget &parent,
			 xxTextMode &caller,
			 constString initFilename,
			 Timestamp initStartTime,
			 Timestamp initEndTime,
			 const Dstr &timezone);
  ~xxRareModeSavePrompts();

  void callback();

protected:
  xxTextMode &_caller;
  Dstr _timezone;
  std::auto_ptr<xxHorizDialog> filenameDiag;
  std::auto_ptr<xxTimestampDialog> beginTimeDiag, endTimeDiag;
  std::auto_ptr<xxWidget> helpLabel, goButton, cancelButton, spaceLabel;
};

// Cleanup2006 Done
