// $Id: xxFilename.hh 4499 2012-05-27 02:30:42Z flaterco $

/*  xxFilename  Get a file name and format from the user.  If successful, do
    caller.save (filename, format).

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

class xxFilename: public xxWindow, public xxReconfigurable {
public:
  xxFilename (const xxWidget &parent,
	      xxPredictionWindow &caller,
	      bool textOnly);
  ~xxFilename();

  void callback();
  void reconfigure();

protected:
  xxPredictionWindow &_caller;
  const bool _textOnly;
  std::auto_ptr<xxHorizDialog> filenameDiag;
  std::auto_ptr<xxWidget> goButton, cancelButton;
  std::auto_ptr<xxMultiChoice> formatChoice;
};
