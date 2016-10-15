// $Id: xxWindow.hh 2641 2007-09-02 21:31:02Z flaterco $

/*  xxWindow  An XTide window.

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

class xxWindow: public xxRedrawable {
public:

  // Normal constructor for a window needing a popup and maybe a
  // container.
  enum ContainerType {noContainer, boxContainer, formContainer};
  xxWindow (const xxWidget &parent,
            ContainerType containerType,
            XtGrabKind grabKind = XtGrabNone);

  // One-time-only constructor to produce a top-level application
  // shell, which is weird and different in lots of ways.  Command
  // line switches recognized by X11 are removed from argc and argv.
  xxWindow (int &argc, char **argv);

  ~xxWindow();


  virtual void realize();             // Show the window.
  void move (const Dstr &geometry);   // Move the window.
  virtual void unrealize();           // Hide the window.
  void globalRedraw();                // See xxRedrawable.

  // Get rid of the window (almost).
  // The returned pointer must be deleted by the caller.
  // It is considered harmful for objects to delete themselves.
  // See xxRoot and xxLocationList for why this can't be simpler.
  virtual xxWindow * const dismiss() warnUnusedResult;

  // Set to true to prevent user from closing window at inopportune times.
  bool noClose;

protected:
  const ContainerType _containerType;
  const XtGrabKind _grabKind;
  std::auto_ptr<xxWidget> popup, container;
  Window _iconWindow;
  bool _isRealized;

  // Radius (in pixels) affected by right mouse click in location
  // choosers.
  static const unsigned rightClickRadius = 15U;

  // How far (in pixels) outside the location chooser window do we
  // draw to avoid chopping off lines.
  static const int antiChopMargin = 50;


  // Install default icon.  Only works on old fashioned window managers.
  void setIcon();
  void setTitle (const Dstr &title);  // Set window title and icon name.

  // Restrict resizing of the window.  Doable only while the window is
  // realized; is forgotten when the window is unrealized.
  void fixSize();
  void widthNoSmaller();
  void setMinSize (Dimension width, Dimension height);

  // If possible, use the NET_WM protocol to set the window title
  // correctly on systems expecting a UTF-8 encoding.  Indirection
  // through _title necessary because the NET_WM property can only be
  // changed while the window is realized.
  Dstr _title;
  void setTitle_NET_WM();
};

// Cleanup2006 Done
