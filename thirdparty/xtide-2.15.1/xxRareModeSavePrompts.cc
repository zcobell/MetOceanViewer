// $Id: xxRareModeSavePrompts.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxRareModeSavePrompts  Get a file name and two timestamps from the
    user.  If successful, do caller.save (filename, startTime, endTime).

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
#include "xxHorizDialog.hh"
#include "xxMultiChoice.hh"
#include "xxTimestampDialog.hh"
#include "xxTimestamp.hh"
#include "xxRareModeSavePrompts.hh"
#include "xxTextMode.hh"


static void xxRareModeSavePromptsCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  xxRareModeSavePrompts *prompts ((xxRareModeSavePrompts *)client_data);
  prompts->callback();
  delete prompts->dismiss();
}


void xxRareModeSavePrompts::callback() {
  Dstr filename (filenameDiag->val());
  if (filename.strchr ('\n') != -1 ||
      filename.strchr ('\r') != -1 ||
      filename.strchr (' ') != -1 ||
      filename[0] == '-') {
    Dstr details ("Well, it's not that I can't do it, it's that you probably\n\
don't want me to.  The filename that you entered is '");
    details += filename;
    details += "'.\n";
    if (filename[0] == '-')
      details += "Filenames that begin with a dash are considered harmful.";
    else
      details += "Whitespace in filenames is considered harmful.";
    Global::barf (Error::CANT_OPEN_FILE, details, Error::nonfatal);
  } else {
    Dstr startTimeString, endTimeString;
    beginTimeDiag->val (startTimeString);
    Timestamp startTime (startTimeString, _timezone);
    endTimeDiag->val (endTimeString);
    Timestamp endTime (endTimeString, _timezone);
    if (startTime.isNull())
      Global::cant_mktime (startTimeString, _timezone, Error::nonfatal);
    else if (endTime.isNull())
      Global::cant_mktime (endTimeString, _timezone, Error::nonfatal);
    else {
      if (startTime <= endTime)
        _caller.save (filename, startTime, endTime);
      else
        _caller.save (filename, endTime, startTime);
    }
  }
}


static void xxRareModeSavePromptsCancelCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxRareModeSavePrompts *)client_data)->dismiss();
}


xxRareModeSavePrompts::~xxRareModeSavePrompts() {
  unrealize();
  _caller.noClose = false;
}


xxRareModeSavePrompts::xxRareModeSavePrompts (const xxWidget &parent,
					      xxTextMode &caller,
					      constString initFilename,
					      Timestamp initStartTime,
					      Timestamp initEndTime,
					      const Dstr &timezone):
  xxWindow (parent, boxContainer, XtGrabExclusive),
  _caller(caller),
  _timezone(timezone) {

  _caller.noClose = true;
  setTitle ("Save prompts");

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
    Dstr helptext ("\
Please enter the name of the file in which to save output.\n\
You can also change the begin and end times for the listing.\n\
The active time zone is ");
    if (Global::settings["z"].c == 'n')
      helptext += _timezone;
    else
      helptext += "UTC0";
    helptext += ".";
    Widget labelWidget = xxX::createXtWidget (helptext.aschar(),
      labelWidgetClass, container->widget(), labelArgs, 3);
    helpLabel = xxX::wrap (labelWidget);
  }

  filenameDiag = std::auto_ptr<xxHorizDialog> (new xxHorizDialog (
                                                                *container,
							        "Filename:",
							        initFilename));
  beginTimeDiag = std::auto_ptr<xxTimestampDialog> (new xxTimestampDialog (
                                                                 *container,
							         "Start at:",
							         initStartTime,
							         _timezone));
  endTimeDiag = std::auto_ptr<xxTimestampDialog> (new xxTimestampDialog (
                                                                   *container,
							           "End at:",
							           initEndTime,
							           _timezone));

  {
    Widget labelWidget = xxX::createXtWidget ("  ", labelWidgetClass,
      container->widget(), labelArgs, 3);
    spaceLabel = xxX::wrap (labelWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Go", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxRareModeSavePromptsCallback,
     (XtPointer)this);
    goButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Cancel", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback,
      xxRareModeSavePromptsCancelCallback, (XtPointer)this);
    cancelButton = xxX::wrap (buttonWidget);
  }

  realize();
}

// Cleanup2006 Done
