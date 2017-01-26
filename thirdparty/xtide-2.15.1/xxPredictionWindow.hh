// $Id: xxPredictionWindow.hh 5034 2013-07-08 01:58:13Z flaterco $

/*  xxPredictionWindow  Abstract class for all tide-predicting windows.

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

class xxPredictionWindow: public xxWindow {
public:

  // The new window takes ownership of station.
  xxPredictionWindow (const xxWidget &shell,
                      Station *station,
                      Timestamp startTime,
                      ContainerType containerType = boxContainer);

  ~xxPredictionWindow();

  // Callbacks.  The ones with no arguments are callbacks from menu
  // options; they create dialogs to get more information.  The ones
  // with arguments are callbacks from the ensuing dialogs.
  virtual void help() = 0;
  virtual void save () = 0;
  virtual void save (const Dstr &filename, Format::Format form) = 0;
  void units();
  void mark ();
  void mark (NullablePredictionValue newMarkLevel);
  void timestamp ();
  void timestamp (Timestamp newTimestamp);
  void aspect ();
  void aspect (double newAspect);
  void step ();
  void step (Interval newStep);

  void globalRedraw();                // See xxRedrawable.

  // Accessors.
  Station * const station() const;
  const Timestamp startTime() const;

protected:

  std::auto_ptr<Station> _station;

  // This is the "starting time" or "now" that is used to calibrate
  // any given drawable in time.  This needs to be here so that it
  // is possible for one drawable to create another one with the
  // same start time.
  Timestamp t;

  std::auto_ptr<xxWidget> saveButton, markButton, helpButton, dismissButton,
                          optionsButton, optionsMenu, graphButton,
                          plainButton, rawButton, mediumRareButton,
                          aboutStationButton, aboutXTideButton, aspectButton,
                          clockButton, chooserButton, timestampButton,
                          unitsButton, rootButton, stepButton;


  virtual const bool isGraph() const;  // true if graph or clock
  virtual const bool isClock() const;  // true if clock
  virtual const bool isRare() const;   // true if medium rare or raw

  // This is called by subclasses to hook up the dismiss button, the help
  // button, and the options menu.  Requires containerType != noContainer.
  // Forms must pass the widgets above and to the left for layout purposes.
  void addNormalButtons (Widget northWidget = NULL, Widget westWidget = NULL);

  virtual void redraw() = 0;
};

// Cleanup2006 Done
