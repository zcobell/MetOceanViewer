// $Id: xxRoot.hh 5034 2013-07-08 01:58:13Z flaterco $

/*  xxRoot  XTide "root" window (control panel, top-level logic)

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

class xxHelpBox;
class xxGraphMode;
class xxTextMode;
class xxClock;

class xxRoot: public xxWindow, public xxMouseWheelViewport {
public:

  // There can be only one instance of xxRoot.
  xxRoot (int argc, char **argv);

  // No destructor because the single instance lasts until exit().

  void run(); // Does not return.

  // Methods to create independent new windows (those using
  // XtGrabNone).  The preferred way for any other class to create
  // such a window is by invoking one of these methods on
  // Global::root.  The resulting window will be a child of root.

  // Those methods that get mutable Station *station assume ownership
  // of the station.

  xxHelpBox * const newHelpBox     (const Dstr &help);
  xxHelpBox * const newAbout       (const Station *station);
  void const newAboutXTide         ();
  xxGraphMode * const newGraph     (Station *station, Timestamp t);
  xxGraphMode * const newGraph     (const StationRef &stationRef);   // t = now
  xxTextMode * const newPlain      (Station *station, Timestamp t);
  xxTextMode * const newRaw        (Station *station, Timestamp t);
  xxTextMode * const newMediumRare (Station *station, Timestamp t);
  xxClock * const newClock         (Station *station,
				    xxClock::ButtonsStyle buttonsStyle);
  xxClock * const newClock         (Station *station);  // style per settings
  void newMap();
  void newGlobe();
  void newChooser();        // Map or Globe according to settings.

  // Other methods.

  void realize();              // Show control panel.
  void unrealize();            // Hide control panel.
  xxWindow * const dismiss();  // Hide control panel, return NULL (no delete).
  void globalRedraw();         // Redraw all windows.

  // Apply and maybe save settings.  These operations are combined to
  // ensure that settings will not be saved unless they can be
  // successfully applied.
  enum ApplyProtocol {justApply, applyAndSave};
  void apply (ApplyProtocol protocol);

  void dup (xxWindow *child);     // Add a child window and call dup().
  void release (xxWindow *child); // Remove a child window and call release().

  // This is needed for the error callback from barf.  Other classes
  // are not expected to invoke it directly.  Does not return if
  // fatality is Fatal.
  void newErrorBox (const Dstr &errmsg, Error::ErrType fatality);

protected:

  // xxRoot keeps track of all windows and propagates globalRedraw to
  // them all when its own globalRedraw is invoked.
  std::set<xxWindow*> children;

  // The number of popups is one more than children.size() when the
  // control panel is visible; otherwise they are equal.
  unsigned popupCount;

  // No need for auto_ptr here because xxRoot remains in scope until exit().
  xxWidget *viewport, *viewBox, *dismissButton, *helpButton, *label,
           *applyButton, *saveButton;

  // Map from switchName to a pointer to an xxMultiChoice, xxHorizDialog,
  // or xxUnsignedChooser, as appropriate.
  BetterMap<const Dstr, xxRedrawable*> dialogs;


  // Start windows as requested on command line.
  void commandLineWindows();

  // This replaces XtAppMainLoop.  Does not return if protocol is
  // loopForever.
  enum HandleXEventsReturnProtocol {returnWhenIdle, loopForever};
  void handleXEvents (HandleXEventsReturnProtocol protocol);

  void dup();     // Increment count of popups.
  void release(); // Decrement count of popups, exit if now zero.
};

// Cleanup2006 Done
