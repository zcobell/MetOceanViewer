// $Id: xxGraphMode.cc 5749 2014-10-11 19:42:10Z flaterco $

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

#include "xtide.hh"
#include "libxtide/Graph.hh"
#include "libxtide/PixelatedGraph.hh"
#include "libxtide/RGBGraph.hh"
#include "libxtide/SVGGraph.hh"
#include "xxPixmapGraph.hh"
#include "xxGraphMode.hh"
#include "xxHorizDialog.hh"
#include "xxMultiChoice.hh"
#include "xxReconfigurable.hh"
#include "xxFilename.hh"


static const double scrollFraction = 0.5;


void xxGraphMode::help() {
  Dstr helpstring ("\
XTide Graph Mode Window\n\
\n\
Use the forward and backward buttons to scroll the graph forward\n\
or backward in time.  You can resize the window to see more or less\n\
detail.\n\
\n\
The options menu supplies the following commands:\n\
\n\
  Save:  Use this to write the tide predictions to an image file.\n\
\n\
  Set Mark:  This is used to set the 'mark level' for tide predictions.\n\
  The prediction window will show the times when the tide level crosses\n\
  the mark.\n\
\n\
  Convert ft<->m:  Convert feet to meters or vice-versa.\n\
\n\
  Set Time:  Go to a different point in time.  Major adjustments that\n\
  cannot be made with the forward and backward buttons can be made with\n\
  Set Time.\n\
\n\
  Set Aspect:  This is used to change the aspect ratio for tide graphing.\n\
  The aspect ratio is a measure of how scrunched up or stretched out\n\
  the graph is.  If tide events are too close together, increase the\n\
  aspect; if you want to fit more information onto one graph, decrease\n\
  the aspect.\n\
\n\
  New Graph Window:  Duplicates the existing graph window.\n\
\n\
  New Plain Mode Window:  Open a window with a text listing of tide\n\
  predictions for the current location and time.\n\
\n\
  New Raw Mode Window:  Open a window with a text listing of unprocessed\n\
  numerical time stamps and tide levels for the current location and time.\n\
\n\
  New Medium Rare Mode Window:  Open a window with a text listing of\n\
  processed timestamps and unprocessed numerical tide levels for the\n\
  current location and time.\n\
\n\
  New Clock Window:  Open a window with a tide clock for the current\n\
  location.\n\
\n\
  About This Station:  Show station metadata.\n\
\n\
  About XTide:  Show XTide version and GPL.\n\
\n\
  New Location Chooser:  Open new globe and location list windows to allow\n\
  selecting a new location.\n\
\n\
  Control Panel:  Advanced XTide configuration.");
  (void) xxroot->newHelpBox (helpstring);
}


static void xxGraphModeforwardCallback (Widget w unusedParameter,
                                        XtPointer client_data,
                                        XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxGraphMode *)client_data)->forward();
}


const Interval xxGraphMode::increment() const {
  // Cloned from Graph::drawTides--assuming that fudge factor is 1.
  return Interval (std::max ((interval_rep_t)1,
			     Global::intervalround (Global::aspectMagicNumber /
						    (double)curGraphHeight /
						    _station->aspect)));
}


void xxGraphMode::updatePixmap (bool resized) {
  if (xxX::displaySucks) {
    if (resized)
      graph = std::auto_ptr<xxPixmapGraph> (new xxPixmapGraph (curGraphWidth,
							      curGraphHeight));
    graph->drawTides (_station.get(), t);
  } else {
    RGBGraph g (curGraphWidth, curGraphHeight);
    g.drawTides (_station.get(), t);
    if (havePixmap)
      XFreePixmap (xxX::display, pixmap);
    havePixmap = true;
    pixmap = xxX::makePixmap (curGraphWidth, curGraphHeight, g.rgb);
  }
}


void xxGraphMode::forward() {
  t += increment() * (scrollFraction * curGraphWidth);
  updatePixmap (false);
  draw();
}


static void xxGraphModebackwardCallback (Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxGraphMode *)client_data)->backward();
}


void xxGraphMode::backward() {
  t -= increment() * (scrollFraction * curGraphWidth);
  updatePixmap (false);
  draw();
}


static void xxGraphModeResizeHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (event);
  if (event->type == ConfigureNotify) {
    assert (client_data);
    XConfigureEvent *ev = (XConfigureEvent *)event;
    ((xxGraphMode *)client_data)->resize (ev->height, ev->width);
  }
}


static void exposureHandler (Widget w unusedParameter,
			     XtPointer client_data,
			     XEvent *event,
			     Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  assert (event);
  ((xxGraphMode *)client_data)->draw ((XExposeEvent*)event);
}


void xxGraphMode::resize (Dimension newHeight, Dimension newWidth) {
  if (newHeight != curWindowHeight || newWidth != curWindowWidth) {
    curWindowHeight = newHeight;
    curWindowWidth = newWidth;
    curGraphHeight = newHeight - (origWindowHeight - origGraphHeight);
    curGraphWidth = newWidth - (origWindowWidth - origGraphWidth);
    redraw();
  }
}


void xxGraphMode::save () {
  (void) new xxFilename (*popup, *this, false);
}


void xxGraphMode::save (const Dstr &filename, Format::Format form) {
  switch (form) {
  case Format::PNG:
    if ((Global::PNGFile = fopen (filename.aschar(), "wb"))) {
      RGBGraph g (curGraphWidth, curGraphHeight);
      g.drawTides (_station.get(), t);
      g.writeAsPNG (Global::writePNGToFile);
      fclose (Global::PNGFile);
    } else
      Global::cantOpenFile (filename, Error::nonfatal);
    break;
  case Format::SVG:
    {
      FILE *fp;
      if ((fp = fopen (filename.aschar(), "w"))) {
	Dstr text;
	SVGGraph g (curGraphWidth, curGraphHeight);
	g.drawTides (_station.get(), t);
	g.print (text);
	fprintf (fp, "%s", text.aschar());
	fclose (fp);
      } else
	Global::cantOpenFile (filename, Error::nonfatal);
    }
    break;
  default:
    assert (false);
  }
}


xxGraphMode::xxGraphMode (const xxWidget &shell,
			  Station *station,
			  Timestamp startTime):
  xxPredictionWindow (shell, station, startTime) {
  construct();
}


xxGraphMode::xxGraphMode (const xxWidget &shell,
			  Station *station):
  xxPredictionWindow (shell, station, (time_t)time(NULL)) {
  construct();
}


void xxGraphMode::construct () {
  havePixmap = false;
  Dstr title (_station->name);
  title += " (Graph)";
  setTitle (title);
  XtAddEventHandler (popup->widget(), StructureNotifyMask, False,
		     xxGraphModeResizeHandler, (XtPointer)this);

  Dimension minxgwidth = xxX::minWidthFudgeFactor * 5 + xxX::stringWidth
    (xxX::defaultFontStruct, "BackwardForwardOptionsDismiss?");

  origGraphHeight = curGraphHeight = Global::settings["gh"].u;
  origGraphWidth = curGraphWidth = std::max (
                                          (Dimension)Global::settings["gw"].u,
					  minxgwidth);
  Arg labelArgs[6] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {XtNheight, (XtArgVal)origGraphHeight},
    {XtNwidth, (XtArgVal)origGraphWidth},
    {XtNinternalHeight, (XtArgVal)0},
    {XtNinternalWidth, (XtArgVal)0}
  };
  {
    Widget labelWidget = xxX::createXtWidget ("",
      labelWidgetClass, container->widget(), labelArgs, 6);
    label = xxX::wrap (labelWidget);
    XtAddEventHandler (labelWidget, ExposureMask, False,
	  	       exposureHandler, (XtPointer)this);
  }
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  {
    Widget buttonWidget = xxX::createXtWidget ("Backward",
      repeaterWidgetClass, container->widget(), buttonArgs, 2);
    XtAddCallback (buttonWidget, XtNcallback, xxGraphModebackwardCallback,
     (XtPointer)this);
    backwardButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Forward",
      repeaterWidgetClass, container->widget(), buttonArgs, 2);
    XtAddCallback (buttonWidget, XtNcallback, xxGraphModeforwardCallback,
     (XtPointer)this);
    forwardButton = xxX::wrap (buttonWidget);
  }

  addNormalButtons();
  realize();
  redraw();

  {
    Arg args[2] = {
      {XtNwidth, (XtArgVal)(&origWindowWidth)},
      {XtNheight, (XtArgVal)(&origWindowHeight)}
    };
    XtGetValues (container->widget(), args, 2);
    curWindowHeight = origWindowHeight;
    curWindowWidth = origWindowWidth;
  }

  setMinSize (minxgwidth + (origWindowWidth - origGraphWidth),
	      Global::minGraphHeight + (origWindowHeight - origGraphHeight));
}


void xxGraphMode::redraw() {
  updatePixmap (true);
  Arg labelArgs[2] =  {
    {XtNheight, (XtArgVal)curGraphHeight},
    {XtNwidth, (XtArgVal)curGraphWidth}
  };
  XtSetValues (label->widget(), labelArgs, 2);
  draw();
}


void xxGraphMode::draw (int x, int y, int width, int height) {
  if ((havePixmap || graph.get()) && _isRealized) {
    XCopyArea (xxX::display,
	       (havePixmap ? pixmap : graph->pixmap),
	       XtWindow(label->widget()),
	       xxX::textGC,
	       x,
	       y,
	       width,
	       height,
	       x,
	       y);
  }
}


void xxGraphMode::draw (const XExposeEvent *exposeEvent) {
  draw (exposeEvent->x,
	exposeEvent->y,
	exposeEvent->width,
	exposeEvent->height);
}


void xxGraphMode::draw () {
  draw (0, 0, curGraphWidth, curGraphHeight);
}


xxGraphMode::~xxGraphMode() {
  unrealize();
  if (havePixmap)
    XFreePixmap (xxX::display, pixmap);
}


const bool xxGraphMode::isGraph() const {
  return true;
}


void xxGraphMode::globalRedraw() {
  xxPredictionWindow::globalRedraw();
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (backwardButton.get());
  XtSetValues (backwardButton->widget(), buttonArgs, 2);
  assert (forwardButton.get());
  XtSetValues (forwardButton->widget(), buttonArgs, 2);
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (label.get());
  XtSetValues (label->widget(), args, 2);
}

// Cleanup2006 Semiclone(xxClock) Done
