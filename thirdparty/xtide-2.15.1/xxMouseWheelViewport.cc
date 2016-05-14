// $Id: xxMouseWheelViewport.cc 4501 2012-05-27 17:41:58Z flaterco $

/*  xxMouseWheelViewport  Nontrivial callback to make a viewport
    responsive to the mouse wheel (and keyboard).  There might be an
    easier way.

    Copyright (C) 2007  David Flater.

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


xxMouseWheelViewport::xxMouseWheelViewport() {}


xxMouseWheelViewport::~xxMouseWheelViewport() {}


static void mouseButtonCallback (Widget w unusedParameter,
                                 XtPointer client_data,
                                 XEvent *event,
                                 Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  ((xxMouseWheelViewport*)client_data)->mouseButton ((XButtonEvent*)event);
}


static void keyboardCallback (Widget w unusedParameter,
			      XtPointer client_data,
			      XEvent *event,
			      Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  // Index 0 = no shift/ctrl/meta/etc.
  ((xxMouseWheelViewport*)client_data)->keyboard (XLookupKeysym ((XKeyEvent *)event, 0));
}


void xxMouseWheelViewport::keyboard (KeySym key) {
  switch (key) {
  case XK_Up:
  case XK_KP_Up:
    scroll (UpLine);
    break;
  case XK_Down:
  case XK_KP_Down:
    scroll (DownLine);
    break;
  case XK_Page_Up:
  case XK_KP_Page_Up:
    scroll (UpPage);
    break;
  case XK_Page_Down:
  case XK_KP_Page_Down:
    scroll (DownPage);
    break;
  case XK_Home:      // That's "Home" not "Begin" ...
  case XK_KP_Home:
    scroll (WayUp);
    break;
  case XK_End:
  case XK_KP_End:
    scroll (WayDown);
    break;
  default:
    return;
  }
}


void xxMouseWheelViewport::mouseButton (const XButtonEvent *xbe) {
  assert (xbe);
  switch (xbe->button) {
  case Button4:
    scroll (UpPage);
    break;
  case Button5:
    scroll (DownPage);
    break;
  default:
    return;
  }
}


void xxMouseWheelViewport::scroll (HowScroll how) {
  if (how == WayUp || how == WayDown) {
    float top = (how == WayDown ? 1.0 : 0.0);
    XtCallCallbacks (scrollbarWidget, XtNjumpProc, &top);
  } else {
    int pixels;
    if (how == UpPage || how == DownPage) {
      Dimension scrollbarLength;
      Arg args[1] = {
	{XtNlength, (XtArgVal)(&scrollbarLength)}
      };
      XtGetValues (scrollbarWidget, args, 1);
      pixels = scrollbarLength;
      if (how == UpPage)
	pixels = -pixels;
    } else {
      pixels = 2*xxFontHeight(xxX::monoFontStruct);
      if (how == UpLine)
	pixels = -pixels;
    }
    // 2012-01-22
    // The StartScroll, NotifyScroll, and EndScroll actions are incompatible
    // between Xaw and Xaw3d.  The following works the same on both.
    // Inappropriately, the parameter type is XtPointer, alias void*; ignore
    // compiler warning "cast to pointer from integer of different size".
    XtCallCallbacks (scrollbarWidget, XtNscrollProc, (XtPointer)pixels);
  }
}


void xxMouseWheelViewport::obeyMouseWheel (Widget widget) {
  XtAddEventHandler (widget, ButtonPressMask, False,
                     mouseButtonCallback, (XtPointer)this);
  XtAddEventHandler (widget, KeyPressMask, False,
		     keyboardCallback, (XtPointer)this);
}
