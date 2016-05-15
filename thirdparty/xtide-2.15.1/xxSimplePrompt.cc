// $Id: xxSimplePrompt.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxSimplePrompt  Generic application of dialogWidgetClass.

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
#include "xxSimplePrompt.hh"


static void xxSimplePromptCallback (Widget w unusedParameter,
				    XtPointer client_data,
				    XtPointer call_data unusedParameter) {
  assert (client_data);
  xxSimplePrompt *prompt ((xxSimplePrompt *)client_data);
  prompt->callback();
  delete prompt->dismiss();
}


void xxSimplePrompt::callback() {
  callback (XawDialogGetValueString (dialog->widget()));
}


static void xxSimplePromptCancelCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxSimplePrompt *)client_data)->dismiss();
}


xxSimplePrompt::~xxSimplePrompt() {
  unrealize();
  _caller.noClose = false;
}


xxSimplePrompt::xxSimplePrompt (const xxWidget &parent,
				xxPredictionWindow &caller,
				const Dstr &title):
  xxWindow (parent, noContainer, XtGrabExclusive),
  _caller(caller) {
  _caller.noClose = true;
  setTitle (title);
}


void xxSimplePrompt::construct (const Dstr &helpText, const Dstr &initValue) {
  Widget dialogWidget;
  {
    Arg dialogArgs[4] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNlabel, (XtArgVal)helpText.aschar()},
      {XtNvalue, (XtArgVal)initValue.aschar()}
    };
    dialogWidget = xxX::createXtWidget ("",
      dialogWidgetClass, popup->widget(), dialogArgs, 4);
    dialog = xxX::wrap (dialogWidget);
  }

  XawDialogAddButton (dialogWidget, "Go", xxSimplePromptCallback,
   (XtPointer)this);
  XawDialogAddButton (dialogWidget, "Cancel", xxSimplePromptCancelCallback,
   (XtPointer)this);

  // Need to force the colors.
  Widget labelWidget  = XtNameToWidget (dialogWidget, "label");
  Widget valueWidget  = XtNameToWidget (dialogWidget, "value");
  Widget goWidget     = XtNameToWidget (dialogWidget, "Go");
  Widget cancelWidget = XtNameToWidget (dialogWidget, "Cancel");
  Arg labelArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  XtSetValues (labelWidget, labelArgs, 2);
  XtSetValues (valueWidget, labelArgs, 2);
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  XtSetValues (goWidget, buttonArgs, 2);
  XtSetValues (cancelWidget, buttonArgs, 2);
  setRudeness (labelWidget);
  setRudeness (valueWidget);
  setRudeness (goWidget);
  setRudeness (cancelWidget);

  realize();
}
