// $Id: xxDisclaimer.hh 5036 2013-07-08 23:15:35Z flaterco $

/*  xxDisclaimer  Annoy the user.

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

class xxDisclaimer: public xxWindow, public xxMouseWheelViewport {
public:

  // "Don't show this again" button is provided only if the second argument
  // is true.
  xxDisclaimer (const xxWidget &shell, bool enableDisableButton);
  ~xxDisclaimer ();

  xxWindow * const dismiss();         // Get rid of the window.
  void globalRedraw();                // See xxRedrawable.

protected:
  std::auto_ptr<xxWidget> dismissButton, dontShowButton, label, viewport;
  Widget horizontalScrollbarWidget;   // Owned by the viewport.
};

// Cleanup2006 Done
