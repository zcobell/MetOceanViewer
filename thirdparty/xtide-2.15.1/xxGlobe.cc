// $Id: xxGlobe.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  xxGlobe   Location chooser using Orthographic Projection.

    There is some duplicated code between xxGlobe and xxMap.  However,
    they are sufficiently different that I think complete
    encapsulation is the cleanest approach.  -- DWF

    Copyright (C) 1998  David Flater.
    (Portions by Jan Depner copied from xxMap.)

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

#include "xtide.hh"
#include "xxReconfigurable.hh"
#include "xxLocationList.hh"
#include "xxGlobe.hh"


// The viewing window will be minGlobeSize + 1 pixels squared.
static const unsigned long minGlobeSize (500UL);

static const unsigned long maxGlobeSize (512000UL);

// Zoom factor of left mouse click.
static const double zoomFactor (2.0);

// This controls the selection of which resolution WVS file to use at a
// given zoom factor.  Smaller numbers result in more aggressive use of
// higher resolution files and therefore more CPU loading on average.
// Useful range is vaguely 1e+12 down to 1e+10.
static const double zoomResolutionMagicNumber (9.0e+10);


// Test for coordinates within reasonable boundaries to avoid
// overflowing X11.
static const bool inRange (long x, long y) {
  return (x >= SHRT_MIN && x <= SHRT_MAX &&
          y >= SHRT_MIN && y <= SHRT_MAX);
}


static void xxGlobePointerMotionEventHandler (
                          Widget w unusedParameter,
                          XtPointer client_data,
                          XEvent *event,
                          Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  ((xxGlobe *)client_data)->mouseMove ((XMotionEvent *)event);
}


void xxGlobe::mouseMove (const XMotionEvent *xme) {
  assert (xme);

  // Coordinates relative to upper left corner of map pixmap.
  // Need to subtract the internal margin of the label widget.
  xLast = xme->x - internalWidth;
  yLast = xme->y - internalHeight;
  updatePosition (xLast, yLast);
}


// This drawCoastlines is based on Jan's redrawMap.
void xxGlobe::drawCoastlines () {

  static constString files[6] = {"wvsfull.dat",
                                 "wvs250k.dat",
                                 "wvs1.dat", 
                                 "wvs3.dat",
                                 "wvs12.dat",
                                 "wvs43.dat"};

  // Scales / resolutions as described in wvsrtv.cc (1:X)
  // wvsfull is unknown but doesn't matter.
  static const unsigned long scales[6] = {0UL,
                                          250000UL,
                                          1000000UL,
                                          3000000UL,
                                          12000000UL,
                                          43000000UL};

  // First figure out the worst case bounds.
  int startlat, endlat, startlon, endlon;
  if (centerLatitude == 0.0) {
    startlat = -90;
    endlat = 90;
    // Add 1 because of double-to-int roundoff.
    startlon = (int)centerLongitude - 91;
    if (startlon < -180)
      startlon += 360;
    endlon = (int)centerLongitude + 91;
    if (endlon > 180)
      endlon -= 360;
  } else {
    // Any time that the globe is tilted, a pole could be showing
    // and we could need the entire range of longitudes.
    startlon = -180;
    endlon = 180;
    if (centerLatitude > 0.0) {
      // Add 1 because of double-to-int roundoff.
      startlat = -91 + (int)centerLatitude;
      if (startlat < -90)
        startlat = -90;
      endlat = 90;
    } else {
      startlat = -90;
      endlat = 91 + (int)centerLatitude;
      if (endlat > 90)
        endlat = 90;
    }
  }

  // OK, here we go.
  // The following code will break if the viewport is not centered.
  // There were too many special cases to do it otherwise.
  // (What is left still contains some overkill.)

  // If any of the corners is out in space, stop right there.  We are
  // probably fully zoomed out and can't improve the worst case anyhow.
  double ullat, ullon, urlat, urlon, lllat, lllon, lrlat, lrlon;
  double ulat, llat, junklon;
  bool forgetit (false);
  if (!untranslateCoordinates (0, 0, ullat, ullon))
    forgetit = true;
  else if (!untranslateCoordinates (0, minGlobeSize, lllat, lllon))
    forgetit = true;
  else if (!untranslateCoordinates (minGlobeSize, 0, urlat, urlon))
    forgetit = true;
  else if (!untranslateCoordinates (minGlobeSize, minGlobeSize,
                                    lrlat, lrlon))
    forgetit = true;
  // The extrema for latitude can be in the middle.
  else if (!untranslateCoordinates (minGlobeSize/2, 0, ulat, junklon))
    forgetit = true;
  else if (!untranslateCoordinates (minGlobeSize/2, minGlobeSize,
                                    llat, junklon))
    forgetit = true;

  if (!forgetit) {

#define boundmacro(lbound,ubound,x) \
  if (x<lbound) lbound=x; else if (x>ubound) ubound=x;

    double dminlat (lllat);
    double dmaxlat (lllat);
    boundmacro (dminlat, dmaxlat, ullat);
    boundmacro (dminlat, dmaxlat, lrlat);
    boundmacro (dminlat, dmaxlat, urlat);
    boundmacro (dminlat, dmaxlat, ulat);
    boundmacro (dminlat, dmaxlat, llat);
    double dminlon (lllon);
    double dmaxlon (lllon);
    boundmacro (dminlon, dmaxlon, ullon);
    boundmacro (dminlon, dmaxlon, lrlon);
    boundmacro (dminlon, dmaxlon, urlon);

    int minlat (std::max ( -90, (int)(floor(dminlat))));
    int maxlat (std::min (  90, (int)(ceil(dmaxlat))));
    int minlon (std::max (-180, (int)(floor(dminlon))));
    int maxlon (std::min ( 180, (int)(ceil(dmaxlon))));

    // If a pole is showing, then forget longitude and just work on
    // latitude.  The most expedient way to find if a pole is showing is
    // to translate its coordinates.
    long junkx, junky;
    if (translateCoordinates (90.0, 0.0, junkx, junky)) // N
      startlat = std::max (startlat, minlat);
    else if (translateCoordinates (-90.0, 0.0, junkx, junky)) // S
      endlat = std::min (endlat, maxlat);
    else {
      // Otherwise, we can narrow the bounds every which way, but watch out
      // for that accursed special case (x3).
      startlat = std::max (startlat, minlat);
      endlat = std::min (endlat, maxlat);
      if (ullon > urlon && lllon > lrlon) {
        startlon = std::max (startlon, (int)(floor(std::min(lllon, ullon))));
        endlon = std::min (endlon, (int)(ceil(std::max(lrlon, urlon))));
      } else if (ullon > urlon) {
        startlon = std::max (startlon, (int)(floor(ullon)));
        endlon = std::min (endlon, (int)(ceil(urlon)));
      } else if (lllon > lrlon) {
        startlon = std::max (startlon, (int)(floor(lllon)));
        endlon = std::min (endlon, (int)(ceil(lrlon)));
      } else {
        startlon = std::max (startlon, minlon);
        endlon = std::min (endlon, maxlon);
      }
    }
  }

  // In Jan's code where he sets bounds (xxMap::keyboard), the special
  // case where we cross 180 and west is numerically higher than east
  // is addressed by adding 360 to east.  So, we'll do that here.
  if (startlon > endlon)
    endlon += 360;

  float *latray(NULL), *lonray(NULL);
  int *segray(NULL), coast, lat, lon;
  long segx[2], segy[2];

  /* Get the proper coastline data set based on the zoom level. */
  // Your propriety may vary.
  // Originally, 3 was skipped because Jan didn't like having it
  // around because the file is physically larger than 2.
  for (coast=5;
       coast > 0 && (double)size * (double)scales[coast] > zoomResolutionMagicNumber;
       --coast);

  /*  Read and plot the coastlines. */

  for (lat = startlat ; lat < endlat ; ++lat) {
    for (lon = startlon ; lon < endlon ; ++lon) {
      int k, nseg, offset(0);

      nseg = wvsrtv (files[coast], WVSdir(), lat, lon,
		     &latray, &lonray, &segray);

      if (nseg) {
        for (k = 0 ; k < nseg ; ++k) {
          int cnt, m(0);
          for (cnt = 0 ; cnt < segray[k] ; ++cnt) {
            // Need to fix out-of-range longitudes here.  We get them
            // because of the workaround discussed above.
            double fixedlon ((double)lonray[offset+cnt]);
            if (fixedlon < -180.0)
              fixedlon += 360.0;
            else if (fixedlon > 180.0)
              fixedlon -= 360.0;
            if (translateCoordinates ((double)latray[offset+cnt],
                                      fixedlon,
                                      segx[m],
                                      segy[m])) {
              // In xxMap, Jan did this:
              /*  Check for the weird situation when
                  west and east are at 0.0 and 360.0.  */
              //                if (m && segx[1] - segx[0] < 50 && 
              //                    segx[0] - segx[1] < 50) {
              // Whatever that was supposed to do in xxMap, it wreaks
              // havoc here.  (Lines vanish when zoomed in.)
              if (m) {
                // On sphere, this should be an arc.  (Nit)
                XDrawLine (xxX::display, globePixmap, xxX::textGC, 
                           segx[0], segy[0], segx[1], segy[1]);
                segx[0] = segx[1];
                segy[0] = segy[1];
              }
              m = 1;
            } else
              m = 0;
          }
          offset += segray[k];
        }
      }
    }
  }
  wvsrtv ("clean", NULL, 0, 0, &latray, &lonray, &segray);
}


// Based on Jan's
void xxGlobe::updatePosition (long rx, long ry) {
  char   string[50];
  double lat, lon;
  if (untranslateCoordinates (rx, ry, lat, lon)) {
    sprintf (string, "Lat %8.4f", lat);
    Arg args[1] = {{XtNlabel, (XtArgVal)string}};
    XtSetValues (latitudeLabel->widget(), args, 1);
    sprintf (string, "Lon %9.4f", lon);
    XtSetValues (longitudeLabel->widget(), args, 1);
  }
}


void xxGlobe::drawGridlines () {

  // Draw a circle around everything just for looks.
  if (inRange (-xOrigin, -yOrigin) &&
      inRange (size - xOrigin, size - yOrigin))
    XDrawArc (xxX::display, globePixmap, xxX::textGC,
              -xOrigin, -yOrigin, size, size, 0, 360*64);

  // I was hoping to determine the arcs analytically but gave myself a
  // headache trying to figure out the longitude arcs.  The following
  // implementation uses brute force and ignorance to draw a faceted
  // version.  Surprisingly, it seems to be good enough and fast
  // enough.

  // Longitude
  long lng, lat, lastx=0, lasty=0, newx, newy;
  for (lng = -180; lng <= 150; lng += 30) {
    bool lastvalid = false;
    for (lat = -90; lat <= 90; lat += 5) {
      if (projectCoordinates (lat, lng, newx, newy)) {
        // This draws farther outside the realm than TranslateCoordinates
        // would allow.
        if (lastvalid)
          if (inRange (lastx-xOrigin, lasty-yOrigin) &&
              inRange (newx-xOrigin, newy-yOrigin))
            XDrawLine (xxX::display, globePixmap, xxX::textGC,
                       lastx-xOrigin, lasty-yOrigin,
                       newx-xOrigin, newy-yOrigin);
        lastx = newx;
        lasty = newy;
        lastvalid = true;
      } else
        lastvalid = false;
    }
  }

  // Latitude
  for (lat = -60; lat <= 60; lat += 30) {
    bool lastvalid = false;
    for (lng = -180; lng <= 180; lng += 5) {
      if (projectCoordinates (lat, lng, newx, newy)) {
        // This draws farther outside the realm than TranslateCoordinates
        // would allow.
        if (lastvalid)
          if (inRange (lastx-xOrigin, lasty-yOrigin) &&
              inRange (newx-xOrigin, newy-yOrigin))
            XDrawLine (xxX::display, globePixmap, xxX::textGC,
                       lastx-xOrigin, lasty-yOrigin,
                       newx-xOrigin, newy-yOrigin);
        lastx = newx;
        lasty = newy;
        lastvalid = true;
      } else
        lastvalid = false;
    }
  }
}


const bool xxGlobe::projectCoordinates (double lat,
                                        double lng,
                                        long &x_out,
                                        long &y_out) {
  // wvsrtv sent longitude -180.002060
  assert (lat >= -90 && lat <= 90 && lng >= -181 && lng <= 180);

  // Calibrate longitude.
  lng -= centerLongitude;
  // Don't know how to calibrate latitude with spherical coordinates so
  // I'll do that later.

  // Using formulas from:
  // Daniel Zwillinger, ed., Standard Mathematical Tables and Formulae,
  // 30th ed., CRC Press.

  // Section 4.9.4.
  // Convert spherical coordinates to Cartesian coordinates.
  // (The foo_ prefix is to remind me that the x and y axes in the
  // formulas are NOT the same as my x and y axes.)
  double zenith  = (90.0 - lat) * M_PI / 180.0;
  double azimuth = lng * M_PI / 180.0;
  double foo_x = cos (azimuth) * sin (zenith);
  double foo_y = sin (azimuth) * sin (zenith);
  double foo_z = cos (zenith);

  // Section 4.10.1, Formulas for Symmetries: Cartesian Coordinates.
  // Calibrate latitude by rotation around (0,1,0).
  // Collapsing all the zeroes and ones out of the formula we get:
  {
    double sinalpha = sin (centerLatitude * M_PI / 180.0);
    double cosalpha = cos (centerLatitude * M_PI / 180.0);
    double bar_x = cosalpha * foo_x + sinalpha * foo_z;
    double bar_z = -sinalpha * foo_x + cosalpha * foo_z;
    foo_x = bar_x;
    foo_z = bar_z;
  }

  if (foo_x < 0.0)
    return false; // It's around the other side
  double s2 = (double)size/2.0;
  x_out = Global::lround (foo_y * s2 + s2);
  y_out = Global::lround (-foo_z * s2 + s2);
  return true;
}


const bool xxGlobe::projectCoordinates (const Coordinates &coordinates,
                                        long &x_out,
                                        long &y_out) {
  if (coordinates.isNull())
    return false;
  return projectCoordinates (coordinates.lat(),
                             coordinates.lng(),
                             x_out,
                             y_out);
}


// translateCoordinates calls projectCoordinates then further cooks the
// result to map it into the current viewport.
const bool xxGlobe::translateCoordinates (double lat,
                                          double lng,
                                          long &x_out,
                                          long &y_out) {
  assert (lat >= -90 && lat <= 90 && lng >= -180 && lng <= 180);
  if (!projectCoordinates (lat, lng, x_out, y_out))
    return false;
  x_out -= xOrigin;
  y_out -= yOrigin;
  // Allow a cushy margin so that lines won't get chopped.
  return (x_out >= -antiChopMargin &&
          x_out <= (long)minGlobeSize+antiChopMargin &&
          y_out >= -antiChopMargin &&
          y_out <= (long)minGlobeSize+antiChopMargin);
}


const bool xxGlobe::translateCoordinates (const Coordinates &coordinates,
                                          long &x_out,
                                          long &y_out) {
  if (coordinates.isNull())
    return false;
  return translateCoordinates (coordinates.lat(),
                               coordinates.lng(),
                               x_out,
                               y_out);
}


const bool xxGlobe::untranslateCoordinates (long x,
                                            long y,
                                            double &lat_out,
                                            double &lng_out) {

  if (x < 0 || x > (long)minGlobeSize || y < 0 || y > (long)minGlobeSize)
    return false;

  // Undo translation to window coordinates.
  x += xOrigin;
  y += yOrigin;

  // See comments in projectCoordinates about "foo coordinates" and the
  // formulas below.

  // Convert back to foo coordinates, inferring depth axis (foo_x).
  double s2    = (double)size/2.0;
  double foo_y = ((double)x-s2)/s2;
  double foo_z = -((double)y-s2)/s2;
  double foo_x = 1.0 - foo_y * foo_y - foo_z * foo_z; // Temporarily squared
  if (foo_x < 0.0)
    return false; // Outside the circle = miss.
  foo_x = sqrt (foo_x);

  // Undo latitude rotation (same formula, but negate the angle).
  {
    double sinalpha = sin (-centerLatitude * M_PI / 180.0);
    double cosalpha = cos (-centerLatitude * M_PI / 180.0);
    double bar_x = cosalpha * foo_x + sinalpha * foo_z;
    double bar_z = -sinalpha * foo_x + cosalpha * foo_z;
    foo_x = bar_x;
    foo_z = bar_z;
  }

  // Convert Cartesian coordinates back to spherical coordinates.
  // (Also in Section 4.10.1)
  // Denominator in second zenith formula is always 1 for us.
  double azimuth = atan2 (foo_y, foo_x);  // Range -pi to pi
  double zenith  = acos (foo_z);          // Range 0 to pi

  // Convert spherical coordinates back to lat and lng.
  lat_out = 90.0 - zenith * 180.0 / M_PI;     // Range -90 to 90
  lng_out = azimuth * 180.0 / M_PI;           // Range -180 to 180

  // Undo center longitude adjustment.
  lng_out += centerLongitude;
  if (lng_out > 180.0)
    lng_out -= 360.0;
  else if (lng_out < -180.0)
    lng_out += 360.0;
  assert (lng_out >= -180.0 && lng_out <= 180.0);

  return true;
}


void xxGlobe::drawCurrentPosition() {
  long x, y;
  if (translateCoordinates (currentPosition, x, y)) {
    XDrawLine (xxX::display, globePixmap, xxX::textGC, x, y-4, x, y+4);
    XDrawLine (xxX::display, globePixmap, xxX::textGC, x-4, y, x+4, y);
  }
}


// If you support zooming and panning by drawing the globe bigger and
// parenting it with a viewport, you run out of memory VERY fast.
// Zoom 10x = 5000x5000x3 bytes = 75 MEGS

void xxGlobe::redrawGlobe () {
  Cursor cursor = XCreateFontCursor (xxX::display, XC_watch);
  XDefineCursor (xxX::display, XtWindow (picture->widget()), cursor);

  // 2010-04-06 DWF
  // The viewable area is now always centered on the translated globe.
  xOrigin = yOrigin = (size - minGlobeSize)/2;

  XFillRectangle (xxX::display, globePixmap, xxX::backgroundGC,
                  0, 0, minGlobeSize+1, minGlobeSize+1);

  drawCoastlines();
  drawGridlines();
  drawCurrentPosition();

  // Draw station blobs
  StationIndex *si (new StationIndex());
  int spot, halfspot;
  if (size < maxGlobeSize / 64) {
    spot = 3;
    halfspot = 1;
  } else if (size < maxGlobeSize / 16) {
    spot = 5;
    halfspot = 2;
  } else {
    spot = 7;
    halfspot = 3;
  }
  for (unsigned long i=0; i<stationIndex.size(); ++i) {
    long x, y;
    if (translateCoordinates (stationIndex[i]->coordinates, x, y)) {
      // Here, we *do* want to eliminate everything not strictly in
      // the window, modulo the size of the dots.
      if (x >= -halfspot &&
          y >= -halfspot &&
          x <= (long)minGlobeSize+halfspot &&
          y <= (long)minGlobeSize+halfspot) {
        XFillArc (xxX::display, globePixmap,
             (stationIndex[i]->isCurrent ? xxX::currentdotGC : xxX::tidedotGC),
                  x-halfspot, y-halfspot, spot, spot, 0, 360*64);
        si->push_back (stationIndex[i]);
      }
    }
  }

  // Update lat/lng readouts
  updatePosition (xLast, yLast);

  // Install new pixmap and station list
  Arg args[1] = {{(char*)"bitmap", (XtArgVal)globePixmap}};
  XtSetValues (picture->widget(), args, 1);
  blastFlag = false;
  if (locationList.get())
    locationList->changeList (si);
  else
    locationList = std::auto_ptr<xxLocationList> (new xxLocationList (*popup,
                                                                      si,
                                                                      this));

  XUndefineCursor (xxX::display, XtWindow (picture->widget()));
  XFreeCursor (xxX::display, cursor);
}


static void dismissCallback (Widget w unusedParameter,
                             XtPointer client_data,
                             XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxGlobe *)client_data)->dismiss();
}


static void xxGlobeFlatCallback (Widget w unusedParameter,
                                 XtPointer client_data,
                                 XtPointer call_data unusedParameter) {
  assert (client_data);
  xxroot->newMap ();
  delete ((xxGlobe *)client_data)->dismiss();
}


static void xxGlobeHelpCallback (Widget w unusedParameter,
                                 XtPointer client_data unusedParameter,
                                 XtPointer call_data unusedParameter) {
  Dstr helpstring ("\
XTide Location Chooser -- Globe Window\n\
\n\
The globe window initially shows an entire hemisphere.  Tide stations\n\
in that hemisphere are projected onto the globe as dots and enumerated\n\
in the location list window.  Other tide stations can be accessed by\n\
rotating the globe.\n\
\n\
Mouse buttons:\n\
\n\
  Left:  zoom in on the clicked region and narrow the location list.\n\
  You can zoom in 6 times for a maximum 64x magnification, after which\n\
  the left mouse button behaves just like the right mouse button.\n\
\n\
  Right:  narrow the location list to the clicked area.  A circle will\n\
  be drawn on the globe showing the radius included, but no zooming\n\
  will occur.\n\
\n\
  Middle:  select a tide station.  (You can also select a tide station\n\
  by left-clicking on it in the location list.)\n\
\n\
Buttons in the globe window:\n\
\n\
  List All:  include all available tide stations in the location list,\n\
  even those whose latitude and longitude are unknown (null).\n\
\n\
  Zoom Out:  self-explanatory.  Sufficient usage will return to the\n\
  hemisphere view.\n\
\n\
  Flat:  change to a flat map projection.\n\
\n\
  Dismiss:  remove the location chooser.  Any windows containing tide\n\
  predictions will remain.\n\
\n\
Keyboard:\n\
\n\
  Arrow keys:  rotate North/South/East/West.");
  (void) xxroot->newHelpBox (helpstring);
}


static void xxGlobeListAllCallback (Widget w unusedParameter,
                                    XtPointer client_data,
                                    XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxGlobe *)client_data)->listAll();
}


void xxGlobe::listAll() {
  locationList->changeList (new StationIndex (stationIndex));
}


static void xxGlobeZoomOutCallback (Widget w unusedParameter,
                                    XtPointer client_data,
                                    XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxGlobe *)client_data)->zoomOut();
}


void xxGlobe::zoomOut() {
  if (size == minGlobeSize)
    return;
  size = std::max (minGlobeSize, (unsigned long)(size / zoomFactor));
  redrawGlobe();
}


// This is done by the flat window image and not by spherical projection.
// It's simpler this way.
void xxGlobe::blast (long x, long y) {
  if (blastFlag)
    if (inRange (xBlast, yBlast) &&
        inRange (xBlast+rightClickRadius*2, yBlast+rightClickRadius*2))
      XDrawArc (xxX::display, globePixmap, xxX::invertGC, xBlast, yBlast,
                rightClickRadius*2, rightClickRadius*2, 0, 360*64);

  blastFlag = true;
  xBlast = x - rightClickRadius;
  yBlast = y - rightClickRadius;
  if (inRange (xBlast, yBlast) &&
      inRange (xBlast+rightClickRadius*2, yBlast+rightClickRadius*2))
    XDrawArc (xxX::display, globePixmap, xxX::invertGC, xBlast, yBlast,
              rightClickRadius*2, rightClickRadius*2, 0, 360*64);

  Arg args[1] = {{(char*)"bitmap", (XtArgVal)globePixmap}};
  XtSetValues (picture->widget(), args, 1);

  // Make a list of all locations within the blast radius.
  const long bx (x + xOrigin);
  const long by (y + yOrigin);
  StationIndex *si (new StationIndex());
  for (unsigned long a=0; a<stationIndex.size(); ++a) {
    long x, y;
    if (projectCoordinates (stationIndex[a]->coordinates, x, y)) {
      const double dx = (double)x - (double)bx;
      const double dy = (double)y - (double)by;
      if (dx*dx + dy*dy <= rightClickRadius*rightClickRadius)
        si->push_back (stationIndex[a]);
    }
  }
  locationList->changeList (si);
}


static void xxGlobeKeyboardEventHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  // Index 0 = no shift/ctrl/meta/etc.
  ((xxGlobe *)client_data)->keyboard (XLookupKeysym ((XKeyEvent *)event, 0));
}


void xxGlobe::keyboard (KeySym key) {
  double lat, lng;
  switch (key) {
  case XK_Left:
  case XK_KP_Left:
    require (untranslateCoordinates (minGlobeSize/4, minGlobeSize/2,
                                     lat, lng));
    centerLongitude = lng;
    break;
  case XK_Up:
  case XK_KP_Up:
    require (untranslateCoordinates (minGlobeSize/2, minGlobeSize/4,
                                     lat, lng));
    if (lat > centerLatitude)
      centerLatitude = lat;
    else
      centerLatitude = 90;
    break;
  case XK_Right:
  case XK_KP_Right:
    require (untranslateCoordinates (3*minGlobeSize/4, minGlobeSize/2,
                                     lat, lng));
    centerLongitude = lng;
    break;
  case XK_Down:
  case XK_KP_Down:
    require (untranslateCoordinates (minGlobeSize/2, 3*minGlobeSize/4,
                                     lat, lng));
    if (lat < centerLatitude)
      centerLatitude = lat;
    else
      centerLatitude = -90;
    break;
  default:
    return;
  }
  redrawGlobe();
}


static void xxGlobeButtonPressEventHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  ((xxGlobe*)client_data)->mouseButton ((XButtonEvent*)event);
}


void xxGlobe::mouseButton (const XButtonEvent *xbe) {
  assert (xbe);
  assert (xbe->type == ButtonPress);

  // Coordinates relative to upper left corner of globe pixmap.
  // Need to subtract the internal margin of the label widget.
  long rx = xbe->x - internalWidth;
  long ry = xbe->y - internalHeight;

  // Relative to entire globe.
  long bx = rx + xOrigin;
  long by = ry + yOrigin;

  if (xbe->button == Button3 ||
      (xbe->button == Button1 && size == maxGlobeSize)) {
    blast (rx, ry);
    return;
  }

  // Zoom
  if (xbe->button == Button1) {
    double lat, lng;
    if (untranslateCoordinates (rx, ry, lat, lng)) {
      centerLatitude = lat;
      centerLongitude = lng;
      size = std::min (maxGlobeSize, (unsigned long)(zoomFactor * size));
      redrawGlobe();
    }
    return;
  }

  // Load location
  if (xbe->button == Button2) {
    // Find nearest location that is close enough (4 pixels).
    StationRef *closestsr (NULL);
    double d (17.0);
    for (unsigned long a=0; a<stationIndex.size(); ++a) {
      long x, y;
      if (projectCoordinates (stationIndex[a]->coordinates, x, y)) {
        double dx = (double)x - (double)bx;
        double dy = (double)y - (double)by;
        double dd = dx*dx + dy*dy;
        if (dd < d) {
          d = dd;
          closestsr = stationIndex[a];
        }
      }
    }
    if (closestsr)
      xxroot->newGraph (*closestsr);
  }
}


xxGlobe::xxGlobe (const xxWidget &shell):
  xxWindow (shell, formContainer),
  stationIndex(Global::stationIndex()),
  size(minGlobeSize),
  xOrigin(0),
  yOrigin(0),
  blastFlag(false) {

  setTitle ("Globe");
  globePixmap = xxX::makePixmap (minGlobeSize+1, minGlobeSize+1);

  {
    Arg args[7] = {
      {(char*)XtNbitmap, (XtArgVal)globePixmap},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainTop},
      {(char*)XtNbottom, (XtArgVal)XawChainTop}
    };
    Widget pictureWidget = xxX::createXtWidget ("", labelWidgetClass,
      container->widget(), args, 7);
    picture = xxX::wrap (pictureWidget);
    XtAddEventHandler (pictureWidget, PointerMotionMask, False,
      xxGlobePointerMotionEventHandler, (XtPointer)this);
    XtAddEventHandler (pictureWidget, ButtonPressMask, False,
      xxGlobeButtonPressEventHandler, (XtPointer)this);
    XtAddEventHandler (pictureWidget, KeyPressMask, False,
      xxGlobeKeyboardEventHandler, (XtPointer)this);
  }{
    Arg args[2] = {
      {XtNinternalHeight, (XtArgVal)(&internalHeight)},
      {XtNinternalWidth, (XtArgVal)(&internalWidth)}
    };
    XtGetValues (picture->widget(), args, 2);
  }

  Arg reusedArgs[8] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {(char*)XtNleft, (XtArgVal)XawChainLeft},
    {(char*)XtNright, (XtArgVal)XawChainLeft},
    {(char*)XtNtop, (XtArgVal)XawChainBottom},
    {(char*)XtNbottom, (XtArgVal)XawChainBottom},
    {(char*)XtNfromVert, (XtArgVal)picture->widget()},
    {(char*)XtNfromHoriz, (XtArgVal)NULL}
  };
  {
    Widget buttonWidget = xxX::createXtWidget ("List All",
      commandWidgetClass, container->widget(), reusedArgs, 7);
    XtAddCallback (buttonWidget, XtNcallback, xxGlobeListAllCallback,
      (XtPointer)this);
    listAllButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("Zoom Out",
      commandWidgetClass, container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxGlobeZoomOutCallback,
      (XtPointer)this);
    zoomOutButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("Flat", commandWidgetClass,
      container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxGlobeFlatCallback,
      (XtPointer)this);
    flatButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("Dismiss", commandWidgetClass,
      container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, dismissCallback,
      (XtPointer)this);
    dismissButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("?", commandWidgetClass,
      container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxGlobeHelpCallback,
      (XtPointer)this);
    helpButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    reusedArgs[0].value = (XtArgVal)xxX::pixels[Colors::background];
    Widget latitudeLabelWidget = xxX::createXtWidget ("Lat -00.0000",
      labelWidgetClass, container->widget(), reusedArgs, 8);
    latitudeLabel = xxX::wrap (latitudeLabelWidget);
    reusedArgs[7].value = (XtArgVal)latitudeLabelWidget;
  }{
    Widget longitudeLabelWidget = xxX::createXtWidget ("Lon -000.0000",
      labelWidgetClass, container->widget(), reusedArgs, 8);
    longitudeLabel = xxX::wrap (longitudeLabelWidget);
  }

  realize();
  fixSize();

  PositioningSystem (currentPosition);
  if (currentPosition.isNull()) {
    const Configurable &gl = Global::settings["gl"];
    if (gl.isNull || gl.d == 360.0)
      centerLongitude = stationIndex.bestCenterLongitude();
    else
      centerLongitude = gl.d;
    centerLatitude = 0.0;
  } else {
    // Got a GPS fix; start mostly zoomed in on current location.
    centerLongitude = currentPosition.lng();
    centerLatitude  = currentPosition.lat();
    size = std::max (minGlobeSize, (unsigned long)(maxGlobeSize / zoomFactor));
  }

  redrawGlobe();
}


xxGlobe::~xxGlobe () {
  unrealize();
  XFreePixmap (xxX::display, globePixmap);
}


void xxGlobe::globalRedraw() {
  xxWindow::globalRedraw();
  redrawGlobe();
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (flatButton.get());
  XtSetValues (flatButton->widget(), buttonArgs, 2);
  assert (dismissButton.get());
  XtSetValues (dismissButton->widget(), buttonArgs, 2);
  assert (listAllButton.get());
  XtSetValues (listAllButton->widget(), buttonArgs, 2);
  assert (helpButton.get());
  XtSetValues (helpButton->widget(), buttonArgs, 2);
  assert (zoomOutButton.get());
  XtSetValues (zoomOutButton->widget(), buttonArgs, 2);
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (latitudeLabel.get());
  XtSetValues (latitudeLabel->widget(), args, 2);
  assert (longitudeLabel.get());
  XtSetValues (longitudeLabel->widget(), args, 2);
  assert (picture.get());
  XtSetValues (picture->widget(), args, 2);
}

// Cleanup2006 BadVariableNaming Semiclone(xxMap) CloseEnough
