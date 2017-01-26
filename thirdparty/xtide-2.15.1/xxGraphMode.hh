// $Id: xxGraphMode.hh 3999 2011-08-27 20:47:27Z flaterco $

/*  xxGraphMode  Tide graphs in a window.

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

class xxGraphMode: public xxPredictionWindow {
public:
  xxGraphMode (const xxWidget &shell, Station *station);
  xxGraphMode (const xxWidget &shell, Station *station, Timestamp startTime);
  ~xxGraphMode();

  void forward();
  void backward();
  void resize (Dimension newHeight, Dimension newWidth);
  void help();
  void save ();
  void save (const Dstr &filename, Format::Format form);
  void draw (const XExposeEvent *exposeEvent);

  void globalRedraw();  // See xxRedrawable.

protected:
  Dimension origWindowHeight, origWindowWidth, origGraphHeight,
    origGraphWidth, curGraphHeight, curGraphWidth, curWindowHeight,
    curWindowWidth;
  std::auto_ptr<xxWidget> label, forwardButton, backwardButton;
  bool havePixmap;
  Pixmap pixmap;                       // Used only if !displaySucks
  std::auto_ptr<xxPixmapGraph> graph;  // Used only if displaySucks

  void construct ();      // Common code from multiple constructors.

  const bool isGraph() const;
  void draw (int x,       // Copy part of pixmap into window.
	     int y,       // ints for compatibility with XExposeEvent.
	     int width,
	     int height);
  void draw ();           // Copy entire pixmap into window.
  void redraw();          // Recreate the pixmap, then invoke draw().

  // Multiplex between xxPixmapGraph (graph) and RGBGraph (pixmap).
  void updatePixmap (bool resized);

  const Interval increment() const;
};

// Cleanup2006 Done
