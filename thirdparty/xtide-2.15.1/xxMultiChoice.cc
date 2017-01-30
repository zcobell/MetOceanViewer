// $Id: xxMultiChoice.cc 4502 2012-05-27 17:46:59Z flaterco $

/*  xxMultiChoice  Multiple-choice button widget.

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
#include "xxReconfigurable.hh"
#include "xxLocationList.hh"
#include "xxMultiChoice.hh"


const unsigned xxMultiChoice::choice() const {
  return currentChoice;
}


static void buttonCallback (Widget w,
                            XtPointer client_data,
			    XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxMultiChoice*)client_data)->callback (w);
}


void xxMultiChoice::callback (Widget choiceButton) {
  currentChoice = buttonChoices[choiceButton];
  Arg args[1] = {{XtNlabel, (XtArgVal)(_choices[currentChoice])}};
  XtSetValues (button->widget(), args, 1);
  if (_caller)
    _caller->reconfigure();
}


void xxMultiChoice::construct (Widget containerWidget) {
  Arg buttonArgs[4] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };

  // The menu.
  {
    // 2012-02-04
    // After the location list container was changed from box to form, the
    // sort key button no longer resized itself, with the result that "Sort
    // by Longitude" would no longer fit.  Consequently, the button is now
    // fixed at the width that is required for the longest string that it
    // must contain.

    Arg menuArgs[6] = {
      {XtNvisual, (XtArgVal)xxX::visual},
      {XtNcolormap, (XtArgVal)xxX::colormap},
      {(char*)"menuName", (XtArgVal)"menu"},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNresize, (XtArgVal)false}
    };
    Widget buttonWidget = xxX::createXtWidget (_choices[currentChoice],
      menuButtonWidgetClass, containerWidget, menuArgs, 6);
    button = xxX::wrap (buttonWidget);

    // Only for genuine Athena Widgets is it the case that widget width =
    // string width + 2 * internalWidth.  Xaw3d and Xaw3dXft add different
    // overheads.  Infer the correct width based on what happened with the
    // initial choice.
    const unsigned firstStringWidth (xxX::stringWidth (xxX::defaultFontStruct, _choices[currentChoice]));
    unsigned maxStringWidth = firstStringWidth;
    for (unsigned y=0; _choices[y]; ++y)
      maxStringWidth = std::max (maxStringWidth, xxX::stringWidth (xxX::defaultFontStruct, _choices[y]));
    Dimension currentWidth;
    Arg getWidthArgs[1] = {
      {XtNwidth, (XtArgVal)(&currentWidth)}
    };
    XtGetValues (buttonWidget, getWidthArgs, 1);
    const unsigned correctWidth (maxStringWidth + currentWidth - firstStringWidth);
    if (currentWidth != correctWidth) {
      Arg setWidthArgs[1] = {
	{XtNwidth, (XtArgVal)correctWidth}
      };
      XtSetValues (buttonWidget, setWidthArgs, 1);
    }
  }{
    Widget menushell = XtCreatePopupShell ("menu",
      simpleMenuWidgetClass, button->widget(), buttonArgs, 4);
    menu = xxX::wrap (menushell);
    xxX::fixBorder (menushell);
  }

  // Buttons on the menu.
  for (unsigned y=0; _choices[y]; ++y) {
    Widget buttonWidget = xxX::createXtWidget (_choices[y],
						 smeBSBObjectClass,
						 menu->widget(),
						 buttonArgs,
						 4);
    XtAddCallback (buttonWidget, XtNcallback, buttonCallback, (XtPointer)this);
    choiceButtons.push_back (new xxWidget (buttonWidget));
    buttonChoices[buttonWidget] = y;
  }
}


void xxMultiChoice::construct (const xxWidget &parent, constString caption) {
  {
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNorientation, (XtArgVal)XtorientHorizontal}
    };
    Widget boxWidget = xxX::createXtWidget ("", boxWidgetClass,
      parent.widget(), args, 3);
    _box = xxX::wrap (boxWidget);
  }{
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNborderWidth, (XtArgVal)0}
    };
    Widget labelWidget = xxX::createXtWidget (caption,
      labelWidgetClass, _box->widget(), args, 3);
    label = xxX::wrap (labelWidget);
  }
  construct (_box->widget());
}


xxMultiChoice::xxMultiChoice (const xxWidget &parent,
			      constString caption,
			      constStringArray choices,
			      unsigned initialChoice,
                              xxReconfigurable &caller):
  _caller(&caller),
  _choices(choices),
  currentChoice(initialChoice) {
  construct (parent, caption);
}


xxMultiChoice::xxMultiChoice (const xxWidget &parent,
			      constString caption,
			      constStringArray choices,
			      unsigned initialChoice):
  _caller(NULL),
  _choices(choices),
  currentChoice(initialChoice) {
  construct (parent, caption);
}


xxMultiChoice::xxMultiChoice (const xxWidget &box,
			      constStringArray choices,
			      unsigned initialChoice):
  _caller(NULL),
  _choices(choices),
  currentChoice(initialChoice) {
  construct (box.widget());
}


xxMultiChoice::xxMultiChoice (const xxWidget &box,
			      constStringArray choices,
			      unsigned initialChoice,
			      xxReconfigurable &caller):
  _caller(&caller),
  _choices(choices),
  currentChoice(initialChoice) {
  construct (box.widget());
}


xxMultiChoice::xxMultiChoice (const xxWidget &form,
			      constStringArray choices,
			      unsigned initialChoice,
			      xxReconfigurable &caller,
			      ArgList layoutArgs,
			      Cardinal numLayoutArgs):
  _caller(&caller),
  _choices(choices),
  currentChoice(initialChoice) {
  construct (form.widget());
  if (layoutArgs != NULL && numLayoutArgs > 0)
    XtSetValues (button->widget(), layoutArgs, numLayoutArgs);
}


xxMultiChoice::~xxMultiChoice () {
  for (unsigned a=0; a<choiceButtons.size(); ++a)
    delete choiceButtons[a];
}


const Widget xxMultiChoice::widget() const {
  return button->widget();
}


void xxMultiChoice::globalRedraw() {
  Arg buttonArgs[4] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (button.get());
  XtSetValues (button->widget(), buttonArgs, 4);
  assert (menu.get());
  XtSetValues (menu->widget(), buttonArgs, 4);
  for (unsigned a=0; a<choiceButtons.size(); ++a)
    XtSetValues (choiceButtons[a]->widget(), buttonArgs, 4);
  if (_box.get()) {
    Arg args[2] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
    };
    assert (label.get());
    XtSetValues (label->widget(), args, 2);
    XtSetValues (_box->widget(), args, 2);
  }
}

// Cleanup2006 Done
