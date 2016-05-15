// $Id: xxWindow.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  xxWindow  An XTide window.

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


static void xxWindowCloseHandler (Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  xxWindow *f ((xxWindow *)client_data);
  if (f->noClose)
    return;
  if (event->type == ClientMessage) {
    XClientMessageEvent *ev ((XClientMessageEvent *) event);
    // Window manager close.
    if (ev->message_type == xxX::protocolAtom &&
	ev->data.l[0] == (int)(xxX::killAtom))
      delete f->dismiss();
  }
}


xxWindow::xxWindow (int &argc, char **argv):
  noClose(false),
  _containerType(noContainer),
  _grabKind(XtGrabNone),
  _iconWindow(0),
  _isRealized(false) {

  popup = xxX::wrap (xxX::connect (argc, argv));
  // Can't set colors until after the settings have been processed!
  // See xxRoot::xxRoot.
  XtAddEventHandler (popup->widget(), NoEventMask, True,
    xxWindowCloseHandler, (XtPointer)this);
  setIcon();
}


xxWindow::xxWindow (const xxWidget &parent,
                    ContainerType containerType,
                    XtGrabKind grabKind):
  noClose(false),
  _containerType(containerType),
  _grabKind(grabKind),
  _iconWindow(0),
  _isRealized(false) {

  xxroot->dup (this);
  {
    // I have to specify this stuff explicitly under Openwin.
    // Probably has to do with using non-default colormap.
    Arg args[5] = {
      {XtNdepth, (XtArgVal)xxX::colordepth},
      {XtNvisual, (XtArgVal)xxX::visual},
      {XtNcolormap, (XtArgVal)xxX::colormap},
      {XtNborderColor, (XtArgVal)0},
      {XtNbackground, (XtArgVal)0}
    };
    popup = xxX::wrap (XtCreatePopupShell ("XTide",
					   topLevelShellWidgetClass,
					   parent.widget(),
					   args,
					   5));
  }{
    Arg args[2] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
    };
    XtSetValues (popup->widget(), args, 2);
    switch (_containerType) {
    case noContainer:
      break;
    case boxContainer:
      container = xxX::wrap (xxX::createXtWidget ("",
						    boxWidgetClass,
						    popup->widget(),
						    args,
						    2));
      break;
    case formContainer:
      container = xxX::wrap (xxX::createXtWidget ("",
						    formWidgetClass,
						    popup->widget(),
						    args,
						    2));
      break;
    default:
      assert (false);
    }
  }
  XtAddEventHandler (popup->widget(), NoEventMask, True,
    xxWindowCloseHandler, (XtPointer)this);
  setIcon();
}


xxWindow * const xxWindow::dismiss() {
  return this;
}


// This should never be called for xxRoot since it dies only on exit().
xxWindow::~xxWindow() {
  unrealize();
  xxroot->release (this);
  if (_iconWindow)
    XDestroyWindow (xxX::display, _iconWindow);
}


void xxWindow::move (const Dstr &geometry) {
  int xoff, yoff;
  if (sscanf (geometry.aschar(), "%d%d", &xoff, &yoff) == 2) {
    bool negx ((xoff < 0 || (xoff == 0 && geometry[0] == '-')));
    bool negy ((yoff < 0 || (yoff == 0 && geometry.strrchr('-') > 0)));
    if (negx || negy) {
      Dimension width, height;
      Arg args[2] = {
	{XtNwidth, (XtArgVal)(&width)},
	{XtNheight, (XtArgVal)(&height)}
      };
      XtGetValues (popup->widget(), args, 2);
      if (negx)
	xoff += (int)(DisplayWidth(xxX::display,xxX::screenNum)) - width;
      if (negy)
	yoff += (int)(DisplayHeight(xxX::display,xxX::screenNum)) - height;
    }
    XMoveWindow (xxX::display, XtWindow(popup->widget()), xoff, yoff);
  }
}


void xxWindow::globalRedraw() {
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (popup.get());
  XtSetValues (popup->widget(), args, 2);
  if (container.get())
    XtSetValues (container->widget(), args, 2);
}


void xxWindow::fixSize() {
  assert (_isRealized);
  Dimension width, height;
  Arg args[2] = {
    {XtNwidth, (XtArgVal)&width},
    {XtNheight, (XtArgVal)&height}
  };
  Widget popupWidget (popup->widget());
  XtGetValues (popupWidget, args, 2);
  XSizeHints sizeHint;
  sizeHint.min_width = sizeHint.max_width = width;
  sizeHint.min_height = sizeHint.max_height = height;
  sizeHint.flags = PMinSize | PMaxSize;
  XSetWMNormalHints (xxX::display, XtWindow(popupWidget), &sizeHint);
}


void xxWindow::setMinSize (Dimension width, Dimension height) {
  assert (_isRealized);
  XSizeHints sizeHint;
  sizeHint.min_width = width;
  sizeHint.min_height = height;
  sizeHint.flags = PMinSize;
  XSetWMNormalHints (xxX::display, XtWindow(popup->widget()), &sizeHint);
}


void xxWindow::widthNoSmaller() {
  assert (_isRealized);
  Dimension width;
  Arg args[1] = {
    {XtNwidth, (XtArgVal)&width}
  };
  Widget popupWidget (popup->widget());
  XtGetValues (popupWidget, args, 1);
  XSizeHints sizeHint;
  sizeHint.min_width = width;
  sizeHint.min_height = 0;
  sizeHint.flags = PMinSize;
  XSetWMNormalHints (xxX::display, XtWindow(popupWidget), &sizeHint);
}


void xxWindow::realize() {
  if (!_isRealized) {
    Widget popupWidget (popup->widget());
    XtPopup (popupWidget, _grabKind);
    XSetWMProtocols (xxX::display, XtWindow(popupWidget), &xxX::killAtom, 1);
    _isRealized = true;
    setTitle_NET_WM();
  }
}


void xxWindow::unrealize() {
  if (_isRealized) {
    XtPopdown (popup->widget());
    _isRealized = false;
  }
}


void xxWindow::setIcon() {
  XSetWindowAttributes xswa;
  xswa.background_pixmap = xxX::iconPixmap;
  _iconWindow = XCreateWindow (xxX::display,
                               xxX::rootWindow,
			       0,
                               0,
                               xxX::iconSize,
                               xxX::iconSize,
                               0,
                               xxX::iconColordepth,
                               InputOutput,
                               xxX::iconVisual,
                               CWBackPixmap,
                               &xswa);
  Arg args[1] = {
    {XtNiconWindow, (XtArgVal)_iconWindow}
  };
  XtSetValues (popup->widget(), args, 1);
}


void xxWindow::setTitle (const Dstr &title) {
  _title = "XTide: ";
  _title += title;

  Arg args[2] = {
    {XtNtitle, (XtArgVal)_title.aschar()},
    {XtNiconName, (XtArgVal)_title.aschar()}
  };
  XtSetValues (popup->widget(), args, 2);

  setTitle_NET_WM();
}


// In cases where a non-default locale is in use, the expected
// encoding of XtNtitle is ambiguous.  The Window Manager
// Specification added _NET_WM_NAME to get around this.  It's always
// in UTF-8.  However, not all window managers support it.

void xxWindow::setTitle_NET_WM () {
  if (_isRealized && !_title.isNull()) {
    Dstr temp (_title);
    temp.utf8();
    XChangeProperty (xxX::display,
                     XtWindow(popup->widget()),
		     XInternAtom(xxX::display, "_NET_WM_NAME", False),
		     XInternAtom(xxX::display, "UTF8_STRING", False),
		     8,
                     PropModeReplace,
                     (unsigned char *)temp.aschar(),
                     temp.length());
  }
}

// Cleanup2006 Done
