// $Id: xxUnsignedChooser.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxUnsignedChooser  Let user choose an unsigned value.

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

class xxUnsignedChooser: public xxRedrawable {
public:
  xxUnsignedChooser (const xxWidget &parent,
		     constString caption,
		     unsigned init,
		     bool initIsNull,
		     unsigned minimum);

  void more();
  void less();
  void globalRedraw();                // See xxRedrawable.

  // Accessor.  Returns UINT_MAX for null.
  const unsigned choice() const;

protected:
  unsigned _minimum, currentChoice;
  std::auto_ptr<xxWidget> box, label, numLabel, upButton, downButton;

  void redraw();
};

// Cleanup2006 Done
