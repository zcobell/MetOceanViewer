// $Id: xxTimestamp.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxTimestamp  Get a Timestamp from the user.  If successful, pass it
    to caller.timestamp (newTime).

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
#include "xxMultiChoice.hh"
#include "xxTimestampDialog.hh"
#include "xxTimestamp.hh"


static void xxTimestampCallback (Widget w unusedParameter,
				 XtPointer client_data,
				 XtPointer call_data unusedParameter) {
  assert (client_data);
  xxTimestamp *timestamp ((xxTimestamp *)client_data);
  timestamp->callback();
  delete timestamp->dismiss();
}


void xxTimestamp::callback() {
  Dstr timeString;
  dialog->val (timeString);
  Timestamp t (timeString, _timezone);
  if (t.isNull())
    Global::cant_mktime (timeString, _timezone, Error::nonfatal);
  else
    _caller.timestamp (t);
}


static void xxTimestampCancelCallback (Widget w unusedParameter,
				       XtPointer client_data,
				       XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxTimestamp *)client_data)->dismiss();
}


xxTimestamp::~xxTimestamp() {
  unrealize();
  _caller.noClose = false;
}


xxTimestamp::xxTimestamp (const xxWidget &parent,
			  xxPredictionWindow &caller,
			  Timestamp initTime,
			  const Dstr &timezone):
  xxWindow (parent, boxContainer, XtGrabExclusive),
  _caller(caller),
  _timezone(timezone) {
  _caller.noClose = true;

  assert (!initTime.isNull());

  setTitle ("Select Time");

  Arg labelArgs[3] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {XtNborderWidth, (XtArgVal)0}
  };
  Arg buttonArgs[4] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };

  {
    Dstr f ("Adjust year, month, day, hours, and minutes to desired time in time\n\
zone ");
    if (Global::settings["z"].c == 'n')
      f += _timezone;
    else
      f += "UTC0";
    f += " using 24-hour notation instead of AM/PM.";
    Widget labelWidget = xxX::createXtWidget (f.aschar(), labelWidgetClass,
      container->widget(), labelArgs, 3);
    helpLabel = xxX::wrap (labelWidget);
  }

  dialog = std::auto_ptr<xxTimestampDialog> (new xxTimestampDialog (
								   *container,
								   "Set:",
								   initTime,
								   _timezone));
  {
    Widget labelWidget = xxX::createXtWidget ("  ", labelWidgetClass,
      container->widget(), labelArgs, 3);
    spaceLabel = xxX::wrap (labelWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Go", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxTimestampCallback,
     (XtPointer)this);
    goButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Cancel", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxTimestampCancelCallback,
      (XtPointer)this);
    cancelButton = xxX::wrap (buttonWidget);
  }

  realize();
}

// Cleanup2006 Done
