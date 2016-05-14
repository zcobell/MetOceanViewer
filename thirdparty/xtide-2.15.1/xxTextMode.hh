// $Id: xxTextMode.hh 3959 2011-07-31 18:40:25Z flaterco $

/*  xxTextMode  Raw/medium/plain modes, in a window.

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

class xxTextMode: public xxPredictionWindow {
public:

  // Legal modes are p (plain), m (medium rare), or r (raw).
  xxTextMode (const xxWidget &shell, Station *station, Mode::Mode mode);
  xxTextMode (const xxWidget &shell,
	      Station *station,
	      Mode::Mode mode,
              Timestamp startTime);
  ~xxTextMode();

  void forward();
  void backward();
  void mouseButton (const XButtonEvent *xbe);
  void resize (Dimension newHeight);
  void save ();
  void save (const Dstr &filename, Format::Format form); // plain
  void save (const Dstr &filename,  // rare
	     Timestamp startTime,
	     Timestamp endTime);
  void help();

  void globalRedraw();                // See xxRedrawable.

protected:

  Mode::Mode _mode;
  Dimension origHeight, origLabelHeight, height;
  unsigned lines;
  std::auto_ptr<xxWidget> label, nameLabel, forwardButton, backwardButton;
  Timestamp lastLineTime; // Timestamp from the last event shown.
  TideEventsOrganizer organizer;

  void construct();       // Common code from multiple constructors.
  const bool isRare() const;
  void draw();
  void redraw();

  // Extend the organizer.
  // Precondition:  organizer is not empty.
  // Postcondition:  organizer will have at least one new event in the
  // indicated direction.
  void extendRange (Station::Direction direction);

  // Standard delta for predictTideEvents and extendRange, plain mode.
  // Varies depending on lines but it has a minimum.
  const Interval standardDeltaPlain();

  // Analogous figure (# events) for rare modes.
  const unsigned standardDeltaRare();
};

// Cleanup2006 Done
