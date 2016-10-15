// $Id: xxClock.hh 4014 2011-08-28 02:12:36Z flaterco $

/*  xxClock  Tide clock.

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

class xxPixmapGraph;

class xxClock: public xxPredictionWindow {
public:
  enum ButtonsStyle {buttons, noButtons};
  xxClock (const xxWidget &shell,
           Station *station,
           ButtonsStyle buttonsStyle = noButtons);
  ~xxClock();

  void resize (Dimension newHeight, Dimension newWidth);
  void toggleButtons();
  void clockTick();
  void help();
  void save ();
  void save (const Dstr &filename, Format::Format form);
  void draw (const XExposeEvent *exposeEvent);

  void globalRedraw();  // See xxRedrawable.

protected:
  const ButtonsStyle _buttonsStyle;
  Angle analogAngle;
  XtIntervalId timer;
  Dimension origWindowHeight, origWindowWidth, origGraphHeight,
    origGraphWidth, curGraphHeight, curGraphWidth, curWindowHeight,
    curWindowWidth;
  std::auto_ptr<xxWidget> label;
  bool havePixmap;
  Pixmap pixmap;                       // Used only if !displaySucks
  std::auto_ptr<xxPixmapGraph> graph;  // Used only if displaySucks

  bool iconWindowToggle;
  Pixmap clockIcon;
  Window iconWindowLocal;

  const bool isGraph() const;
  const bool isClock() const;
  void draw (int x,       // Copy part of pixmap into window.
	     int y,       // ints for compatibility with XExposeEvent.
	     int width,
	     int height);
  void draw ();           // Copy entire pixmap into window.
  void redraw();          // Recreate the pixmap, then invoke draw().

  // Multiplex between xxPixmapGraph (graph) and RGBGraph (pixmap).
  void updatePixmap (bool resized);

  void redrawIcon();      // Icon is more complicated.
  void resetTimer();      // Set wakeup call.
};

// Cleanup2006 Done
