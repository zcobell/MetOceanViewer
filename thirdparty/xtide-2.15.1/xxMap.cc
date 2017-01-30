// $Id: xxMap.cc 5749 2014-10-11 19:42:10Z flaterco $

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


    Modified 2002-02-17
    Jan C. Depner
    Naval Oceanographic Office
    depnerj@navo.navy.mil

    Switched from globe to world map using cylindrical equidistant projection
    (that makes it sound like I actually know what I'm doing - it just
    means X/Y).  Added coastlines using the public domain WVS coastline files
    from the NGDC Global Relief Data CD-ROM (I built those in 1989).  For
    more information on the files see wvsrtv.cc.

*/

#include "xtide.hh"
#include "xxReconfigurable.hh"
#include "xxLocationList.hh"
#include "xxMap.hh"


// The viewing window will be x_map_size+1 pixels by y_map_size+1 pixels.
static const unsigned x_map_size = 1000U;
static const unsigned y_map_size = 500U;

// Limit on the horizontal distance that a single line segment can
// travel when plotting coastlines fully zoomed out.  If this amount
// or greater it is instead treated as a broken line.  This is to
// prevent lines from crossing the screen when 360 degrees are
// visible.
static const int max_x_delta = x_map_size / 2;


static void xxMapPointerMotionEventHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  ((xxMap *)client_data)->mouseMove ((XMotionEvent *)event);
}


void xxMap::mouseMove (const XMotionEvent *xme) {
  assert (xme);

  // Coordinates relative to upper left corner of map pixmap.
  // Need to subtract the internal margin of the label widget.
  xLast = xme->x - internalWidth;
  yLast = xme->y - internalHeight;
  updatePosition (xLast, yLast);
}


const bool xxMap::translateCoordinates (const Coordinates &coordinates,
                                        int &x_out,
                                        int &y_out) {
  if (coordinates.isNull())
    return false;
  return translateCoordinates (coordinates.lat(),
                               coordinates.lng(),
                               x_out,
                               y_out);
}


const bool xxMap::translateCoordinates (double lat,
                                        double lng,
                                        int &x_out,
                                        int &y_out) {
  if (bounds[zoomLevel].elon > 180.0 && lng < bounds[zoomLevel].wlon)
    lng += 360.0;
  if (bounds[zoomLevel].wlon < -180.0 && lng > bounds[zoomLevel].elon)
    lng -= 360.0;

  // Allow a cushy margin so that lines won't get chopped.
  if (lat < bounds[zoomLevel].slat - antiChopMargin ||
      lat > bounds[zoomLevel].nlat + antiChopMargin ||
      lng < bounds[zoomLevel].wlon - antiChopMargin ||
      lng > bounds[zoomLevel].elon + antiChopMargin)
    return false;

  x_out = Global::iround ((lng - bounds[zoomLevel].wlon) * x_map_size /
                          (bounds[zoomLevel].elon - bounds[zoomLevel].wlon));
  y_out = Global::iround ((bounds[zoomLevel].nlat - lat) * y_map_size /
                          (bounds[zoomLevel].nlat - bounds[zoomLevel].slat));
  return true;
}


void xxMap::untranslateCoordinates (double &lat_out,
                                    double &lng_out,
                                    int x,
                                    int y) {
  lng_out = bounds[zoomLevel].wlon + (double)x / (double)x_map_size
            * (bounds[zoomLevel].elon - bounds[zoomLevel].wlon);
  lat_out = bounds[zoomLevel].nlat - (double)y / (double)y_map_size
            * (bounds[zoomLevel].nlat - bounds[zoomLevel].slat);
}


void xxMap::updatePosition (int rx, int ry) {
  char   string[50];
  double lat, lon;

  untranslateCoordinates (lat, lon, rx, ry);

  if (lat < -90.0) lat = -90.0;
  if (lat > 90.0)  lat = 90.0;

  if (lon < bounds[zoomLevel].wlon)
    lon = bounds[zoomLevel].wlon;
  if (lon > bounds[zoomLevel].elon)
    lon = bounds[zoomLevel].elon;

  if (lon < -180.0) lon += 360.0;
  if (lon > 180.0)  lon -= 360.0;

  sprintf (string, "Lat %8.4f", lat);
  Arg latargs[1] = {{XtNlabel, (XtArgVal)string}};
  XtSetValues (latitudeLabel->widget(), latargs, 1);

  sprintf (string, "Lon %9.4f", lon);
  Arg lonargs[1] = {{XtNlabel, (XtArgVal)string}};
  XtSetValues (longitudeLabel->widget(), lonargs, 1);
}


void xxMap::drawCurrentPosition() {
  int x, y;
  if (translateCoordinates (currentPosition, x, y)) {
    XDrawLine (xxX::display, mapPixmap, xxX::textGC, x, y-4, x, y+4);
    XDrawLine (xxX::display, mapPixmap, xxX::textGC, x-4, y, x+4, y);
  }
}


void xxMap::redrawMap () {

  static constString files[6] = {"wvsfull.dat",
                                 "wvs250k.dat",
                                 "wvs1.dat", 
                                 "wvs3.dat",
                                 "wvs12.dat",
                                 "wvs43.dat"};

  float          *latray(NULL), *lonray(NULL);
  int            *segray(NULL), coast;
  Cursor         cursor;

  cursor = XCreateFontCursor (xxX::display, XC_watch);
  XDefineCursor (xxX::display, XtWindow (picture->widget()), cursor);

  XFillRectangle (xxX::display, mapPixmap, xxX::backgroundGC,
                  0, 0, x_map_size + 1, y_map_size + 1);

  const int startlat ((int) (bounds[zoomLevel].slat - 1.0));
  const int endlat   ((int) (bounds[zoomLevel].nlat + 1.0));
  const int startlon ((int) (bounds[zoomLevel].wlon - 1.0));
  const int endlon   ((int) (bounds[zoomLevel].elon + 1.0));

  int spot, halfspot, lat, lon, segx[2], segy[2];

  /*  Get the proper coastline data set and spot size based on the zoom
      level.  */

  switch (zoomLevel) {
  case 0:
  default:
    coast = 5;
    spot = 3;
    halfspot = 1;
    break;

  case 1:
    coast = 4;
    spot = 5;
    halfspot = 2;
    break;

  case 2:
    coast = 4;
    spot = 6;
    halfspot = 3;
    break;

  case 3:
    coast = 2;
    spot = 6;
    halfspot = 3;
    break;

  case 4:
    coast = 2;
    spot = 6;
    halfspot = 3;
    break;

  case 5:
    coast = 1;
    spot = 7;
    halfspot = 3;
    break;

  case 6:
    coast = 1;
    spot = 7;
    halfspot = 3;
    break;

  case 7:
    coast = 0;
    spot = 8;
    halfspot = 4;
    break;

  case 8:
    coast = 0;
    spot = 8;
    halfspot = 4;
    break;
  }

  /*  Read and plot the coastlines.  */

  if (redrawZeroLevel || zoomLevel) {
    for (lat = startlat ; lat < endlat ; ++lat) {
      for (lon = startlon ; lon < endlon ; ++lon) {
        int k, nseg, offset(0);

        nseg = wvsrtv (files[coast], WVSdir(), lat, lon,
		       &latray, &lonray, &segray);

        if (nseg) {
          /*  Get rid of single point islands that were required for
              NAVO.  These were created during the decimation
              process.  */

          if (nseg > 2 || latray[0] != latray[1] || lonray[0] != lonray[1]) {
            for (k = 0 ; k < nseg ; ++k) {
              int cnt, m(0);
              for (cnt = 0 ; cnt < segray[k] ; ++cnt) {
                if (translateCoordinates ((double) latray[offset + cnt],
                                          (double) lonray[offset + cnt],
                                          segx[m],
                                          segy[m])) {
                  /*  Check for the weird situation when
                      west and east are at 0.0 and 360.0.  */
                  if (m && (zoomLevel > 0 ||
                      (segx[1] - segx[0] < max_x_delta &&
                       segx[0] - segx[1] < max_x_delta))) {
                    XDrawLine (xxX::display, mapPixmap, xxX::textGC,
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
    }
    wvsrtv ("clean", NULL, 0, 0, &latray, &lonray, &segray);
  } else
    XCopyArea (xxX::display, topPixmap, mapPixmap, xxX::markGC,
               0, 0, x_map_size, y_map_size, 0, 0);

  /*  Draw the grid lines.  */

  for (lon = startlon ; lon < endlon ; ++lon) {
    if (!(lon % 30) && lon > bounds[zoomLevel].wlon) {
      if (translateCoordinates (bounds[zoomLevel].slat, (double) lon,
                                segx[0], segy[0])) {
        translateCoordinates (bounds[zoomLevel].nlat, (double) lon,
                              segx[1], segy[1]);
        XDrawLine (xxX::display, mapPixmap, xxX::textGC,
                   segx[0], segy[0], segx[1], segy[1]);
      }
    }
  }

  for (lat = startlat ; lat < endlat ; ++lat) {
    if (!(lat % 30) && lat > -90 && lat < 90) {
      if (translateCoordinates ((double) lat, bounds[zoomLevel].wlon,
                                segx[0], segy[0])) {
        translateCoordinates ((double) lat, bounds[zoomLevel].elon,
                              segx[1], segy[1]);
        XDrawLine (xxX::display, mapPixmap, xxX::textGC,
                   segx[0], segy[0], segx[1], segy[1]);
      }
    }
  }

  drawCurrentPosition();

  /*  The first time through we want to save the map so we don't have to
      read the file to regen it.  */

  if (redrawZeroLevel) {
    XCopyArea (xxX::display, mapPixmap, topPixmap, xxX::markGC,
               0, 0, x_map_size, y_map_size, 0, 0);
    redrawZeroLevel = false;
  }

  /*  Plot the stations.  */

  StationIndex *si (new StationIndex());
  for (unsigned long i = 0 ; i < stationIndex.size() ; ++i) {
    int x, y;
    if (translateCoordinates (stationIndex[i]->coordinates, x, y)) {
      // Here, we *do* want to eliminate everything not strictly in
      // the window, modulo the size of the dots.
      if (x >= -halfspot &&
          y >= -halfspot &&
          x <= (int)x_map_size+halfspot &&
          y <= (int)y_map_size+halfspot) {
        XFillArc (xxX::display, mapPixmap,
             (stationIndex[i]->isCurrent ? xxX::currentdotGC : xxX::tidedotGC),
                  x - halfspot, y - halfspot, spot, spot, 0, 360*64);
        si->push_back (stationIndex[i]);
      }
    }
  }

  // Update lat/lng readouts.
  updatePosition (xLast, yLast);

  // Install new pixmap and station list
  Arg args[1] = {{(char*)"bitmap", (XtArgVal)mapPixmap}};
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
  delete ((xxMap *)client_data)->dismiss();
}


static void xxMapRoundCallback (Widget w unusedParameter,
                                XtPointer client_data,
                                XtPointer call_data unusedParameter) {
  assert (client_data);
  xxroot->newGlobe();
  delete ((xxMap *)client_data)->dismiss();
}


static void xxMapHelpCallback (Widget w unusedParameter,
                               XtPointer client_data unusedParameter,
                               XtPointer call_data unusedParameter) {
  Dstr helpstring ("\
XTide Location Chooser -- Map Window\n\
\n\
The map window initially shows the entire world (cylindrical equidistant\n\
projection).  Tide stations are projected onto the map as dots and\n\
enumerated in the location list window.\n\
\n\
Mouse buttons:\n\
\n\
  Left:  zoom in on the clicked region and narrow the location list.\n\
  You can zoom in 8 times for a maximum 512X magnification (first zoom\n\
  is times 4), after which the left mouse button behaves just like the\n\
  right mouse button.\n\
\n\
  Right:  narrow the location list to the clicked area.  A circle will\n\
  be drawn on the map showing the radius included, but no zooming\n\
  will occur.\n\
\n\
  Middle:  select a tide station.  (You can also select a tide station\n\
  by left-clicking on it in the location list.)\n\
\n\
Buttons in the map window:\n\
\n\
  List All:  include all available tide stations in the location list,\n\
  even those whose latitude and longitude are unknown (null).\n\
\n\
  Zoom Out:  self-explanatory.  Sufficient usage will return to the\n\
  world map view.\n\
\n\
  Round:  change to a spherical globe projection.\n\
\n\
  Dismiss:  remove the location chooser.  Any windows containing tide\n\
  predictions will remain.\n\
\n\
Keyboard:\n\
\n\
  Arrow keys:  pan up/down/left/right.");
  (void) xxroot->newHelpBox (helpstring);
}


static void xxMapListAllCallback (Widget w unusedParameter,
                                  XtPointer client_data,
                                  XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxMap *)client_data)->listAll();
}


void xxMap::listAll() {
  locationList->changeList (new StationIndex (stationIndex));
}


static void xxMapZoomOutCallback (Widget w unusedParameter,
                                  XtPointer client_data,
                                  XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxMap *)client_data)->zoomOut();
}


void xxMap::zoomOut() {
  if (!zoomLevel) return;
  --zoomLevel;
  redrawMap();
}


// This is done by the flat window image and not by spherical projection.
// It's simpler this way.

void xxMap::blast (int x, int y) {
  if (blastFlag)
    XDrawArc (xxX::display, mapPixmap, xxX::invertGC, xBlast, yBlast,
              rightClickRadius*2, rightClickRadius*2, 0, 360*64);

  blastFlag = true;
  xBlast = x - rightClickRadius;
  yBlast = y - rightClickRadius;
  XDrawArc (xxX::display, mapPixmap, xxX::invertGC, xBlast, yBlast,
            rightClickRadius*2, rightClickRadius*2, 0, 360*64);

  Arg args[1] = {{(char*)"bitmap", (XtArgVal)mapPixmap}};
  XtSetValues (picture->widget(), args, 1);

  // Make a list of all locations within the blast radius.
  StationIndex *si (new StationIndex());
  for (unsigned long a = 0 ; a < stationIndex.size() ; ++a) {
    int bx, by;
    if (translateCoordinates (stationIndex[a]->coordinates, bx, by)) {
      double dx = (double) bx - (double) x;
      double dy = (double) by - (double) y;
      if (dx*dx + dy*dy <= rightClickRadius*rightClickRadius)
        si->push_back (stationIndex[a]);
    }
  }
  locationList->changeList (si);
}


static void xxMapKeyboardEventHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  // Index 0 = no shift/ctrl/meta/etc.
  ((xxMap *)client_data)->keyboard (XLookupKeysym ((XKeyEvent *)event, 0));
}


void xxMap::keyboard (KeySym key) {

  const double xsize ((bounds[zoomLevel].elon - bounds[zoomLevel].wlon));
  const double ysize ((bounds[zoomLevel].nlat - bounds[zoomLevel].slat));

  // 2007-02-18  5% overlap changed to 75% overlap...
  static const double scrollFraction = 0.25;

  switch (key) {
  case XK_Left:
  case XK_KP_Left:
    if (zoomLevel) {
      bounds[zoomLevel].wlon = bounds[zoomLevel].wlon - xsize * scrollFraction;
      bounds[zoomLevel].elon = bounds[zoomLevel].elon - xsize * scrollFraction;
      if (bounds[zoomLevel].wlon < -180.0) {
        bounds[zoomLevel].wlon += 360.0;
        bounds[zoomLevel].elon = bounds[zoomLevel].wlon + xsize;
      }
    } else {
      centerLongitude -= 30.0;
      if (centerLongitude < -180.0)
        centerLongitude += 360.0;
      bounds[0].wlon = centerLongitude - 180.0;
      bounds[0].elon = centerLongitude + 180.0;

      if (bounds[0].wlon < -180.0) bounds[0].wlon += 360.0;
      if (bounds[0].elon > 180.0)  bounds[0].elon -= 360.0;

      if (bounds[0].wlon >= bounds[0].elon)
        bounds[0].elon += 360.0;

      redrawZeroLevel = true;
    }
    break;

  case XK_Up:
  case XK_KP_Up:
    if (zoomLevel) {
      bounds[zoomLevel].slat = bounds[zoomLevel].slat + ysize * scrollFraction;
      bounds[zoomLevel].nlat = bounds[zoomLevel].nlat + ysize * scrollFraction;
      if (bounds[zoomLevel].nlat > 90.0) {
        bounds[zoomLevel].nlat = 90.0;
        bounds[zoomLevel].slat = bounds[zoomLevel].nlat - ysize;
      }
    }
    break;

  case XK_Right:
  case XK_KP_Right:
    if (zoomLevel) {
      bounds[zoomLevel].wlon = bounds[zoomLevel].wlon + xsize * scrollFraction;
      bounds[zoomLevel].elon = bounds[zoomLevel].elon + xsize * scrollFraction;
      if (bounds[zoomLevel].elon > 180.0) {
        bounds[zoomLevel].elon -= 360.0;
        bounds[zoomLevel].wlon = bounds[zoomLevel].elon - xsize;
      }
    } else {
      centerLongitude += 30.0;
      if (centerLongitude > 180.0) centerLongitude -= 360.0;
      bounds[0].wlon = centerLongitude - 180.0;
      bounds[0].elon = centerLongitude + 180.0;

      if (bounds[0].wlon < -180.0) bounds[0].wlon += 360.0;
      if (bounds[0].elon > 180.0)  bounds[0].elon -= 360.0;

      if (bounds[0].wlon >= bounds[0].elon)
        bounds[0].elon += 360.0;

      redrawZeroLevel = true;
    }
    break;

  case XK_Down:
  case XK_KP_Down:
    if (zoomLevel) {
      bounds[zoomLevel].slat = bounds[zoomLevel].slat - ysize * scrollFraction;
      bounds[zoomLevel].nlat = bounds[zoomLevel].nlat - ysize * scrollFraction;
      if (bounds[zoomLevel].slat < -90.0) {
        bounds[zoomLevel].slat = -90.0;
        bounds[zoomLevel].nlat = bounds[zoomLevel].slat + ysize;
      }
    }
    break;

  default:
    return;
  }

  redrawMap();
}


static void xxMapButtonPressEventHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  ((xxMap*)client_data)->mouseButton ((XButtonEvent*)event);
}


// Update state to zoom in on the specified coordinates by one level.
// Extracted from mouseButton 2010-04-06 DWF.

void xxMap::zoomBounds (const double lat, const double lon) {
  assert (zoomLevel < maxZoomLevel);

  double xsize ((bounds[zoomLevel].elon - bounds[zoomLevel].wlon) / 2.0);
  double ysize ((bounds[zoomLevel].nlat - bounds[zoomLevel].slat) / 2.0);

  /*  From level zero to level one I want to zoom in four times
      instead of two.  */

  if (!zoomLevel) {
    xsize /= 2.0;
    ysize /= 2.0;
  }

  /*  Increment the zoom level and compute the new bounds.  */

  ++zoomLevel;

  bounds[zoomLevel].wlon = lon - xsize / 2.0;
  bounds[zoomLevel].elon = lon + xsize / 2.0;

  if (bounds[zoomLevel].wlon < -180.0)
    bounds[zoomLevel].wlon += 360.0;

  if (bounds[zoomLevel].elon > 180.0)
    bounds[zoomLevel].elon -= 360;

  if (bounds[zoomLevel].wlon >= bounds[zoomLevel].elon)
    bounds[zoomLevel].elon += 360.0;

  bounds[zoomLevel].slat = lat - ysize / 2.0;
  bounds[zoomLevel].nlat = lat + ysize / 2.0;

  if (bounds[zoomLevel].slat < -90.0) {
    bounds[zoomLevel].slat = -90.0;
    bounds[zoomLevel].nlat = bounds[zoomLevel].slat + ysize;
  }

  if (bounds[zoomLevel].nlat > 90.0) {
    bounds[zoomLevel].nlat = 90.0;
    bounds[zoomLevel].slat = bounds[zoomLevel].nlat - ysize;
  }
}


void xxMap::mouseButton (const XButtonEvent *xbe) {
  assert (xbe);
  assert (xbe->type == ButtonPress);

  // Coordinates relative to upper left corner of map pixmap.
  // Need to subtract the internal margin of the label widget.
  const int rx (xbe->x - internalWidth);
  const int ry (xbe->y - internalHeight);

  if (xbe->button == Button3 || (xbe->button == Button1 &&
      zoomLevel == maxZoomLevel)) {
    blast (rx, ry);
    return;
  }

  // Zoom

  if (xbe->button == Button1) {
    if (zoomLevel < maxZoomLevel) {
      double lat, lon;
      untranslateCoordinates (lat, lon, rx, ry);
      zoomBounds (lat, lon);
    }
    redrawMap();
    return;
  }

  // Load location

  if (xbe->button == Button2) {
    // Find nearest location that is close enough (4 pixels).
    StationRef *closestsr (NULL);
    double d (17.0);
    for (unsigned long a = 0 ; a < stationIndex.size() ; ++a) {
      int x, y;
      if (translateCoordinates (stationIndex[a]->coordinates, x, y)) {
        const double dx = (double) x - (double) rx;
        const double dy = (double) y - (double) ry;
        const double dd = dx * dx + dy * dy;
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


xxMap::xxMap (const xxWidget &shell):
  xxWindow (shell, formContainer),
  stationIndex(Global::stationIndex()),
  zoomLevel(0) {

  setTitle ("Map");
  blastFlag = false;
  mapPixmap = xxX::makePixmap (x_map_size + 1, y_map_size + 1);
  topPixmap = xxX::makePixmap (x_map_size + 1, y_map_size + 1);

  {
    Arg args[7] = {
      {(char*)XtNbitmap, (XtArgVal)mapPixmap},
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
        xxMapPointerMotionEventHandler, (XtPointer) this);
    XtAddEventHandler (pictureWidget, ButtonPressMask, False,
        xxMapButtonPressEventHandler, (XtPointer) this);
    XtAddEventHandler (pictureWidget, KeyPressMask, False,
        xxMapKeyboardEventHandler, (XtPointer) this);
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
    XtAddCallback (buttonWidget, XtNcallback, xxMapListAllCallback,
        (XtPointer)this);
    listAllButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("Zoom Out",
        commandWidgetClass, container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxMapZoomOutCallback,
        (XtPointer)this);
    zoomOutButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("Round",
        commandWidgetClass, container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxMapRoundCallback,
        (XtPointer)this);
    roundButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("Dismiss",
        commandWidgetClass, container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, dismissCallback,
        (XtPointer)this);
    dismissButton = xxX::wrap (buttonWidget);
    reusedArgs[7].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("?", commandWidgetClass,
        container->widget(), reusedArgs, 8);
    XtAddCallback (buttonWidget, XtNcallback, xxMapHelpCallback,
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

  // Need to initialize the zero level even if a GPS fix is available.

  bounds[0].slat = -90.0;
  bounds[0].nlat = 90.0;

  const Configurable &gl = Global::settings["gl"];
  if (gl.isNull || gl.d == 360.0)
    centerLongitude = stationIndex.bestCenterLongitude();
  else
    centerLongitude = gl.d;

  bounds[0].wlon = centerLongitude - 180.0;
  bounds[0].elon = centerLongitude + 180.0;

  if (bounds[0].wlon < -180.0) bounds[0].wlon += 360.0;
  if (bounds[0].elon > 180.0)  bounds[0].elon -= 360.0;

  if (bounds[0].wlon >= bounds[0].elon) bounds[0].elon += 360.0;

  redrawZeroLevel = true;
  redrawMap();

  PositioningSystem (currentPosition);
  if (!currentPosition.isNull()) {
    // Got a GPS fix; start zoomed in on current location.
    while (zoomLevel < maxZoomLevel)
      zoomBounds (currentPosition.lat(), currentPosition.lng());
    redrawMap();
  }
}


xxMap::~xxMap () {
  unrealize();
  XFreePixmap (xxX::display, mapPixmap);
  XFreePixmap (xxX::display, topPixmap);
}


void xxMap::globalRedraw() {
  xxWindow::globalRedraw();
  redrawMap();
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (roundButton.get());
  XtSetValues (roundButton->widget(), buttonArgs, 2);
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

// Cleanup2006 BadVariableNaming Semiclone(xxGlobe) CloseEnough
