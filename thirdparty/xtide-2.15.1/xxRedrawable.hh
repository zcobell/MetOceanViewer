// $Id: xxRedrawable.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxRedrawable  That which implements globalRedraw().

    Copyright (C) 2007  David Flater.

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

class xxRedrawable {
public:

  virtual ~xxRedrawable();

  // Notify whatever that Global::settings changed.  Windows using
  // XtGrabExclusive need not implement this because the control panel
  // is locked until those windows exit.
  virtual void globalRedraw() = 0;

protected:
  xxRedrawable();

private:
  // Prohibited operations not implemented.
  xxRedrawable (const xxRedrawable &);
  xxRedrawable &operator= (const xxRedrawable &);
};

// Cleanup2006 Done
