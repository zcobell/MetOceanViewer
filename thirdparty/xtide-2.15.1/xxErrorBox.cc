// $Id: xxErrorBox.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxErrorBox  Show error message.

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
#include "xxErrorBox.hh"
#include "errorExclaim.xpm.hh"
#include "errorSunk.xpm.hh"


static void dieCallback (Widget w unusedParameter,
                         XtPointer client_data,
                         XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxErrorBox *)client_data)->dismiss();
}


xxErrorBox::~xxErrorBox() {
  if (_fatality == Error::fatal)
    exit (-1);
  unrealize();
  XFreePixmap (xxX::display, backgroundPixmap);
}


xxErrorBox::xxErrorBox (const xxWidget &shell,
			const Dstr &errorMessage,
			Error::ErrType fatality):
  xxWindow (shell, boxContainer, XtGrabExclusive),
  _fatality(fatality) {

  setTitle ("Error Message");

  Arg backgroundArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };

  if (!(backgroundPixmap = xxX::makePixmap (errorExclaimXPM)))
    backgroundPixmap = xxX::makePixmap (64, 64);
  Arg boxArgs[1] = {
    {XtNbackgroundPixmap, (XtArgVal)backgroundPixmap}
  };
  XtSetValues (container->widget(), boxArgs, 1);

  if (_fatality == Error::fatal) {
    // Since we are just about to exit, there's no need to keep track
    // of the pixmap or widget.
    Pixmap sunkPixmap;
    if (!(sunkPixmap = xxX::makePixmap (errorSunkXPM)))
      sunkPixmap = xxX::makePixmap (64, 64);
    Arg args[5] = {
      {XtNbitmap, (XtArgVal)sunkPixmap},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNinternalHeight, (XtArgVal)0},
      {XtNinternalWidth, (XtArgVal)0}
    };
    xxX::createXtWidget ("", labelWidgetClass, container->widget(), args, 5);
  }

  Widget labelWidget = xxX::createXtWidget (errorMessage.aschar(),
    labelWidgetClass, container->widget(), backgroundArgs, 2);
  label = xxX::wrap (labelWidget);

  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::mark]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::background]}
  };

  {
    constCharPointer buttonLabel;
    if (_fatality == Error::fatal)
      buttonLabel = "Exit";
    else
      buttonLabel = "Oops";
    Widget buttonWidget = xxX::createXtWidget (buttonLabel,
      commandWidgetClass, container->widget(), buttonArgs, 2);
    XtAddCallback (buttonWidget, XtNcallback, dieCallback, (XtPointer)this);
    button = xxX::wrap (buttonWidget);
  }

  realize();
  fixSize();
}

// Cleanup2006 Done
