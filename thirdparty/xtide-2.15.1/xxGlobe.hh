// $Id: xxGlobe.hh 3636 2010-04-06 23:59:23Z flaterco $

/*  xxGlobe   Location chooser using Orthographic Projection.

    There is some duplicated code between xxGlobe and xxMap.  However,
    they are sufficiently different that I think complete
    encapsulation is the cleanest approach.  -- DWF

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

class xxGlobe: public xxWindow {
public:
  xxGlobe (const xxWidget &shell);
  ~xxGlobe ();

  void listAll();
  void zoomOut();
  void keyboard (KeySym key);
  void mouseMove (const XMotionEvent *xme);
  void mouseButton (const XButtonEvent *xbe);
  void globalRedraw();

protected:
  Pixmap globePixmap;
  const StationIndex &stationIndex;
  std::auto_ptr<xxWidget> picture, dismissButton, listAllButton, helpButton,
                          zoomOutButton, latitudeLabel, longitudeLabel,
                          flatButton;
  std::auto_ptr<xxLocationList> locationList;
  unsigned long size;
  long xOrigin, yOrigin, xBlast, yBlast, xLast, yLast;
  bool blastFlag;
  Dimension internalHeight, internalWidth;
  double centerLongitude, centerLatitude; // center of viewable area
  Coordinates currentPosition; // from GPS

  // projectCoordinates does an orthographic projection of size size
  // (declared above).
  // Returns:
  //   true  = the point is on this side; results valid.
  //   false = the point is on the other side; ignore results.
  const bool projectCoordinates (const Coordinates &coordinates,
				 long &x_out,
				 long &y_out);
  const bool projectCoordinates (double lat,
				 double lng,
				 long &x_out,
				 long &y_out);

  // translateCoordinates calls projectCoordinates then further cooks the
  // result to map it into the current viewport.
  // Returns:
  //   true  = the point may be visible; draw it.
  //   false = the point is invisible; ignore results.
  const bool translateCoordinates (const Coordinates &coordinates,
				   long &x_out,
				   long &y_out);
  const bool translateCoordinates (double lat,
				   double lng,
				   long &x_out,
				   long &y_out);

  // Returns:
  //   true  = the point is on the globe somewhere; lat and lng valid.
  //   false = you missed; ignore lat and lng.
  const bool untranslateCoordinates (long x,
				     long y,
				     double &lat_out,
				     double &lng_out);

  void updatePosition (long x, long y);
  void blast (long x, long y);
  void redrawGlobe();
  void drawCoastlines();
  void drawGridlines();
  void drawCurrentPosition();
};

// Cleanup2006 Done
