// $Id: xxMap.hh 3636 2010-04-06 23:59:23Z flaterco $

/*  xxMap   Location chooser using Cylindrical Equidistant projection.

    There is some duplicated code between xxGlobe and xxMap.  However,
    they are sufficiently different that I think complete
    encapsulation is the cleanest approach.  -- DWF

    Copyright (C) 2002  David Flater.

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

class xxMap: public xxWindow {
public:
  xxMap (const xxWidget &shell);
  ~xxMap ();

  void listAll();
  void zoomOut();
  void keyboard (KeySym key);
  void mouseMove (const XMotionEvent *xme);
  void mouseButton (const XButtonEvent *xbe);
  void globalRedraw();

protected:
  Pixmap mapPixmap;
  Pixmap topPixmap;
  const StationIndex &stationIndex;
  std::auto_ptr<xxWidget> picture, dismissButton, listAllButton, helpButton,
                          zoomOutButton, latitudeLabel, longitudeLabel,
                          roundButton;
  std::auto_ptr<xxLocationList> locationList;
  int xBlast, yBlast, xLast, yLast;
  unsigned zoomLevel;
  bool blastFlag, redrawZeroLevel;
  Dimension internalHeight, internalWidth;
  double centerLongitude;
  Coordinates currentPosition; // from GPS

  static const unsigned maxZoomLevel = 8U;

  struct Bounds {
    double slat;
    double nlat;
    double wlon;
    double elon;
  };

  // For reasons known only to Jan, when you zoom out, it remembers
  // where you were when you zoomed in and goes back there instead of
  // zooming out from the present position.
  Bounds bounds[maxZoomLevel + 1];

  // Returns true if coordinates are in the window or close to it.
  const bool translateCoordinates (const Coordinates &coordinates,
				   int &x_out,
				   int &y_out);
  const bool translateCoordinates (double lat,
				   double lng,
				   int &x_out,
				   int &y_out);

  void untranslateCoordinates (double &lat_out,
                               double &lng_out,
                               int x,
                               int y);

  void updatePosition (int x, int y);
  void blast (int x, int y);
  void redrawMap ();
  void drawCurrentPosition ();
  void zoomBounds (const double lat, const double lon);
};

// Cleanup2006 Done
