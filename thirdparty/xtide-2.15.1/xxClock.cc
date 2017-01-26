// $Id: xxClock.cc 5749 2014-10-11 19:42:10Z flaterco $

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

#include "xtide.hh"
#include "libxtide/Graph.hh"
#include "libxtide/PixelatedGraph.hh"
#include "libxtide/RGBGraph.hh"
#include "libxtide/SVGGraph.hh"
#include "xxPixmapGraph.hh"
#include "xxHorizDialog.hh"
#include "xxMultiChoice.hh"
#include "xxReconfigurable.hh"
#include "xxFilename.hh"


// For testing.  If true, clock updates once per second and angle of
// clock hand in the icon is randomized.
static const bool rapidUpdateTest (false);


void xxClock::help() {
  Dstr helpstring ("\
XTide Tide Clock Window\n\
\n\
The tide clock shows the current time and tide level.  It updates once per\n\
minute.  You can resize the window to see more or less detail.\n\
\n\
To remove the buttons and just have a tide clock, click on the graph.  To\n\
bring back the buttons, click on the graph again.\n\
\n\
The options menu supplies the following commands:\n\
\n\
  Save:  Use this to write the tide predictions to an image file.\n\
\n\
  Set Mark:  This is used to set the 'mark level' for tide predictions.\n\
  In clock mode, this only causes a line to be drawn at the mark level.\n\
  To see the times of mark transitions you must switch to graph or text\n\
  mode.\n\
\n\
  Convert ft<->m:  Convert feet to meters or vice-versa.\n\
\n\
  Set Aspect:  This is used to change the aspect ratio for tide graphing.\n\
  The aspect ratio is a measure of how scrunched up or stretched out\n\
  the graph is.  If tide events are too close together, increase the\n\
  aspect; if you want to fit more information onto one graph, decrease\n\
  the aspect.\n\
\n\
  New Graph Window:  Open a graph window for the current location and time.\n\
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
  New Clock Window:  Duplicates the existing clock window.\n\
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


static void xxClockResizeHandler (Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (event);
  if (event->type == ConfigureNotify) {
    assert (client_data);
    XConfigureEvent *ev = (XConfigureEvent *)event;
    ((xxClock *)client_data)->resize (ev->height, ev->width);
  }
}


void xxClock::resize (Dimension newHeight, Dimension newWidth) {
  if (newHeight != curWindowHeight || newWidth != curWindowWidth) {
    curWindowHeight = newHeight;
    curWindowWidth = newWidth;
    curGraphHeight = newHeight - (origWindowHeight - origGraphHeight);
    curGraphWidth = newWidth - (origWindowWidth - origGraphWidth);
    redraw();
  }
}


static void exposureHandler (Widget w unusedParameter,
			     XtPointer client_data,
			     XEvent *event,
			     Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  assert (event);
  ((xxClock *)client_data)->draw ((XExposeEvent*)event);
}


void xxClock::save () {
  (void) new xxFilename (*popup, *this, false);
}


void xxClock::save (const Dstr &filename, Format::Format form) {
  switch (form) {
  case Format::PNG:
    if ((Global::PNGFile = fopen (filename.aschar(), "wb"))) {
      RGBGraph g (curGraphWidth, curGraphHeight, Graph::clock);
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
	SVGGraph g (curGraphWidth, curGraphHeight, Graph::clock);
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


static void xxClockButtonPressEventHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event unusedParameter,
				  Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  xxClock *clock ((xxClock *)client_data);
  clock->toggleButtons();
  delete clock->dismiss();
}


void xxClock::toggleButtons() {
  // This is rather crude, but it's hard to do much better.
  ButtonsStyle bs;
  switch (_buttonsStyle) {
  case buttons:
    bs = noButtons;
    break;
  case noButtons:
    bs = buttons;
    break;
  default:
    assert (0);
  }
  xxroot->newClock (_station.release(), bs);
}


static void xxClockTimerCallback (XtPointer client_data,
                                  XtIntervalId *timerid unusedParameter) {
  assert (client_data);
  ((xxClock *)client_data)->clockTick();
}


void xxClock::updatePixmap (bool resized) {
  if (xxX::displaySucks) {
    if (resized)
      graph = std::auto_ptr<xxPixmapGraph> (new xxPixmapGraph (curGraphWidth,
							       curGraphHeight,
							       Graph::clock));
    graph->drawTides (_station.get(), t, &analogAngle);
  } else {
    RGBGraph g (curGraphWidth, curGraphHeight, Graph::clock);
    g.drawTides (_station.get(), t, &analogAngle);
    if (havePixmap)
      XFreePixmap (xxX::display, pixmap);
    havePixmap = true;
    pixmap = xxX::makePixmap (curGraphWidth, curGraphHeight, g.rgb);
  }
}


void xxClock::clockTick() {
  t = (time_t)time(NULL);
  updatePixmap (false);
  draw();
  redrawIcon();
  resetTimer();
}


void xxClock::resetTimer() {
  unsigned long interval (1000UL);
  if (!rapidUpdateTest)
    interval *= 60 - (time(NULL)%60);
  timer = XtAppAddTimeOut (xxX::appContext,
			   interval,
			   xxClockTimerCallback,
			   this);
}


xxClock::xxClock (const xxWidget &shell,
                  Station *station,
                  ButtonsStyle buttonsStyle):
  xxPredictionWindow (shell,
		      station,
		      (time_t)time(NULL),
		      ((buttonsStyle == noButtons) ? noContainer
		                                   : boxContainer)),
  _buttonsStyle(buttonsStyle),
  iconWindowToggle(true) {

  havePixmap = false;
  Dstr title (_station->name);
  title += " (Clock)";
  setTitle (title);
  XtAddEventHandler (popup->widget(), StructureNotifyMask, False,
		     xxClockResizeHandler, (XtPointer)this);

  Dimension minxcwidth = xxX::minWidthFudgeFactor * 3 + xxX::stringWidth
    (xxX::defaultFontStruct, "OptionsDismiss?");

  origGraphHeight = curGraphHeight = Global::settings["ch"].u;
  if (_buttonsStyle == noButtons)
    origGraphWidth = curGraphWidth = Global::settings["cw"].u;
  else
    origGraphWidth = curGraphWidth = std::max (
                                          minxcwidth,
                                          (Dimension)Global::settings["cw"].u);
  if (_buttonsStyle == noButtons) {
    Arg args[2] = {
      {XtNwidth, (XtArgVal)curGraphWidth},
      {XtNheight, (XtArgVal)curGraphHeight}
    };
    XtSetValues (popup->widget(), args, 2);
    XtAddEventHandler (popup->widget(), ButtonPressMask, False,
		       xxClockButtonPressEventHandler, (XtPointer)this);
    XtAddEventHandler (popup->widget(), ExposureMask, False,
	  	       exposureHandler, (XtPointer)this);
  } else {
    Arg labelArgs[6] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNheight, (XtArgVal)origGraphHeight},
      {XtNwidth, (XtArgVal)origGraphWidth},
      {XtNinternalHeight, (XtArgVal)0},
      {XtNinternalWidth, (XtArgVal)0}
    };
    Widget labelWidget = xxX::createXtWidget ("",
      labelWidgetClass, container->widget(), labelArgs, 6);
    label = xxX::wrap (labelWidget);
    XtAddEventHandler (labelWidget, ButtonPressMask, False,
		       xxClockButtonPressEventHandler, (XtPointer)this);
    XtAddEventHandler (labelWidget, ExposureMask, False,
	  	       exposureHandler, (XtPointer)this);
    addNormalButtons();
  }

  realize();
  redraw();

  {
    Arg args[2] = {
      {XtNwidth, (XtArgVal)(&origWindowWidth)},
      {XtNheight, (XtArgVal)(&origWindowHeight)}
    };
    if (_buttonsStyle == noButtons)
      XtGetValues (popup->widget(), args, 2);
    else
      XtGetValues (container->widget(), args, 2);
  }
  curWindowHeight = origWindowHeight;
  curWindowWidth = origWindowWidth;

  if (_buttonsStyle == noButtons)
    setMinSize (Global::minGraphWidth + (origWindowWidth - origGraphWidth),
		Global::minGraphHeight + (origWindowHeight - origGraphHeight));
  else
    setMinSize (minxcwidth + (origWindowWidth - origGraphWidth),
		Global::minGraphHeight + (origWindowHeight - origGraphHeight));

  clockIcon = xxX::makeIconPixmap (xxX::iconSize, xxX::iconSize);

  // Need two icon windows to minimize flashing on updates.
  iconWindowLocal = XCreateWindow (xxX::display, xxX::rootWindow,
    0, 0, xxX::iconSize, xxX::iconSize, 0, xxX::iconColordepth,
    InputOutput, xxX::iconVisual, 0, NULL);
  redrawIcon();
  resetTimer();
}


// Because the icon window must be a plain X window and not a widget
// (Must be?  Is this true?), there is no way to set an expose
// callback for it.  We must instead set the backing pixmap of the
// icon window to what we want.  Unfortunately, it is difficult to get
// this to update on demand and when it does update it can flash to
// the background color briefly.  Flashing is minimized by using two
// windows instead of just one, but it cannot be eliminated entirely.

void xxClock::redrawIcon() {
  if (rapidUpdateTest)
    analogAngle = Angle (Units::degrees,
			 360.0 * rand() / ((double)RAND_MAX+1.0));

  XFillRectangle (xxX::display, clockIcon, xxX::iconBackgroundGC, 0, 0,
    xxX::iconSize, xxX::iconSize);

  constCharPointer hicap, locap;
  if (_station->isCurrent)
    hicap = "Flood", locap = "Ebb";
  else
    hicap = "High", locap = "Low";
  int c = xxX::iconSize / 2;  // Center
  int d = xxX::iconSize - 17; // Diameter w/ fixed size "Hi" and "Lo"
  int hl = d / 2 - 4;         // Hand length
  XDrawArc (xxX::display, clockIcon, xxX::iconTextGC,
	    8, 8, d, d, 0, 360*64);
  XDrawArc (xxX::display, clockIcon, xxX::iconTextGC,
	    c-1, c-1, 2, 2, 0, 360*64);
  XDrawString (xxX::display, clockIcon, xxX::iconTextGC,
	       c-(xxX::stringWidth(xxX::iconFontStruct, hicap)/2), 7,
	       hicap, strlen(hicap));
  XDrawString (xxX::display, clockIcon, xxX::iconTextGC,
	       c-(xxX::stringWidth(xxX::iconFontStruct, locap)/2),
	       xxX::iconSize, locap, strlen(locap));
  int handx (Global::iround(c + hl * sin (analogAngle)));
  int handy (Global::iround(c - hl * cos (analogAngle)));
  XDrawLine (xxX::display, clockIcon, xxX::iconTextGC,
	     c, c, handx, handy);

  // Toggle between the two icon windows to minimize flashing.
  Window window;
  if (iconWindowToggle) {
    window = _iconWindow;
    iconWindowToggle = false;
  } else {
    window = iconWindowLocal;
    iconWindowToggle = true;
  }

  // Silly walk to force it to update its pixmap.
  XSetWindowAttributes xswa;
  xswa.background_pixmap = None;
  XChangeWindowAttributes (xxX::display, window, CWBackPixmap, &xswa);
  xswa.background_pixmap = clockIcon;
  XChangeWindowAttributes (xxX::display, window, CWBackPixmap, &xswa);
  Arg iconArgs[1] = {{XtNiconWindow, (XtArgVal)(window)}};
  XtSetValues (popup->widget(), iconArgs, 1);
}


void xxClock::redraw() {
  updatePixmap (true);
  if (_buttonsStyle == buttons) {
    Arg labelArgs[2] =  {
      {XtNheight, (XtArgVal)curGraphHeight},
      {XtNwidth, (XtArgVal)curGraphWidth}
    };
    XtSetValues (label->widget(), labelArgs, 2);
  }
  draw();
}


void xxClock::draw (int x, int y, int width, int height) {
  if ((havePixmap || graph.get()) && _isRealized) {
    Widget target = (_buttonsStyle == noButtons ? popup->widget()
		                                : label->widget());
    XCopyArea (xxX::display,
	       (havePixmap ? pixmap : graph->pixmap),
	       XtWindow(target),
	       xxX::textGC,
	       x,
	       y,
	       width,
	       height,
	       x,
	       y);
  }
}


void xxClock::draw (const XExposeEvent *exposeEvent) {
  draw (exposeEvent->x,
	exposeEvent->y,
	exposeEvent->width,
	exposeEvent->height);
}


void xxClock::draw () {
  draw (0, 0, curGraphWidth, curGraphHeight);
}


xxClock::~xxClock() {
  XtRemoveTimeOut (timer);
  unrealize();
  if (havePixmap)
    XFreePixmap (xxX::display, pixmap);
  XDestroyWindow (xxX::display, iconWindowLocal);
  XFreePixmap (xxX::display, clockIcon);
}


const bool xxClock::isGraph() const {
  return true;
}


const bool xxClock::isClock() const {
  return true;
}


void xxClock::globalRedraw() {
  xxPredictionWindow::globalRedraw();
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  if (label.get())
    XtSetValues (label->widget(), args, 2);
  redrawIcon();
}

// Cleanup2006 Semiclone(xxGraphMode) Done
