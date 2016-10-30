// $Id: xxUnsignedChooser.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxUnsignedChooser  Let user choose an unsigned value.

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
#include "xxUnsignedChooser.hh"


const unsigned xxUnsignedChooser::choice() const {
  return currentChoice;
}


static void xxUnsignedChooserUpCallback (Widget w unusedParameter,
					 XtPointer client_data,
					 XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxUnsignedChooser *)client_data)->more();
}


static void xxUnsignedChooserDownCallback (
                                         Widget w unusedParameter,
					 XtPointer client_data,
					 XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxUnsignedChooser *)client_data)->less();
}


void xxUnsignedChooser::more() {
  if (currentChoice == UINT_MAX)
    currentChoice = _minimum;
  else
    ++currentChoice;
  redraw();
}


void xxUnsignedChooser::less() {
  if (currentChoice == UINT_MAX)
    ;
  else if (currentChoice > _minimum) {
    --currentChoice;
    redraw();
  } else {
    currentChoice = UINT_MAX;
    redraw();
  }
}


void xxUnsignedChooser::redraw() {
  char temp[80];
  // The buffer size must remain constant to keep the button from moving
  // out from under the mouse pointer.  With variable width fonts, it
  // might still happen.
  if (currentChoice < UINT_MAX)
    sprintf (temp, "%7u", currentChoice);
  else
    strcpy (temp, "(blank)");
  Arg args[1] = {{XtNlabel, (XtArgVal)temp}};
  XtSetValues (numLabel->widget(), args, 1);
}


xxUnsignedChooser::xxUnsignedChooser (const xxWidget &parent,
				      constString caption,
				      unsigned init,
				      bool initIsNull,
				      unsigned minimum):
  _minimum(minimum) {

  if (initIsNull)
    init = UINT_MAX;
  assert (init >= _minimum);
  currentChoice = init;

  {
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNorientation, (XtArgVal)XtorientHorizontal}
    };
    Widget boxWidget = xxX::createXtWidget ("", boxWidgetClass,
      parent.widget(), args, 3);
    box = xxX::wrap (boxWidget);
  }{
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNborderWidth, (XtArgVal)0}
    };
    Widget labelWidget = xxX::createXtWidget (caption,
      labelWidgetClass, box->widget(), args, 3);
    label = xxX::wrap (labelWidget);
  }{
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNborderWidth, (XtArgVal)0}
    };
    Widget labelWidget = xxX::createXtWidget ("",
      labelWidgetClass, box->widget(), args, 3);
    numLabel = xxX::wrap (labelWidget);
  }
  redraw();

  Arg buttonArgs[4] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };

  {
    Widget buttonWidget = xxX::createXtWidget ("+", repeaterWidgetClass,
      box->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxUnsignedChooserUpCallback,
     (XtPointer)this);
    upButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("-", repeaterWidgetClass,
      box->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxUnsignedChooserDownCallback,
     (XtPointer)this);
    downButton = xxX::wrap (buttonWidget);
  }
}


void xxUnsignedChooser::globalRedraw() {
  Arg buttonArgs[4] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (upButton.get());
  XtSetValues (upButton->widget(), buttonArgs, 4);
  assert (downButton.get());
  XtSetValues (downButton->widget(), buttonArgs, 4);
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (label.get());
  XtSetValues (label->widget(), args, 2);
  assert (numLabel.get());
  XtSetValues (numLabel->widget(), args, 2);
  assert (box.get());
  XtSetValues (box->widget(), args, 2);
}

// Cleanup2006 Done
