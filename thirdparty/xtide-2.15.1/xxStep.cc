// $Id: xxStep.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxStep  Get a step from the user.  If successful, pass it to
    caller.step (newStep).

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
#include "xxStep.hh"


static void xxStepCallback (Widget w unusedParameter,
                            XtPointer client_data,
                            XtPointer call_data unusedParameter) {
  assert (client_data);
  xxStep *step ((xxStep *)client_data);
  step->callback();
  delete step->dismiss();
}


void xxStep::callback() {
  unsigned seconds (hourChoice->choice() * HOURSECONDS +
                    minuteChoice->choice() * 60);
  if (seconds == 0) {
    Dstr details ("The offending input was 0:00.");
    Global::barf (Error::NUMBER_RANGE_ERROR, details, Error::nonfatal);
  } else
    _caller.step (Interval(seconds));
}


static void xxStepCancelCallback (Widget w unusedParameter,
                                  XtPointer client_data,
                                  XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxStep *)client_data)->dismiss();
}


xxStep::~xxStep() {
  unrealize();
  _caller.noClose = false;
}


xxStep::xxStep (const xxWidget &parent,
		xxPredictionWindow &caller,
		Interval initStep):
  xxWindow (parent, boxContainer, XtGrabExclusive),
  _caller(caller) {
  _caller.noClose = true;

  assert (initStep > Global::zeroInterval);
  assert (initStep < Global::day);
  unsigned initSeconds (std::max ((unsigned)initStep.s(), 60U));
  unsigned hours (initSeconds / HOURSECONDS);
  initSeconds %= HOURSECONDS;
  unsigned minutes (initSeconds / 60);
  assert (hours > 0 || minutes > 0);
  assert (hours < 24 && minutes < 60);

  setTitle ("Enter Step");

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
    static constString caption ("Please enter the new step size in hours and minutes.");
    Widget labelWidget = xxX::createXtWidget (caption, labelWidgetClass,
      container->widget(), labelArgs, 3);
    helpLabel1 = xxX::wrap (labelWidget);
  }

  static constString hourChoices[] = {"00",
    "01", "02", "03", "04", "05", "06", "07", "08", "09", "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
    "21", "22", "23", NULL};
  static constString minuteChoices[] = {"00",
    "01", "02", "03", "04", "05", "06", "07", "08", "09", "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
    "21", "22", "23", "24", "25", "26", "27", "28", "29", "30",
    "31", "32", "33", "34", "35", "36", "37", "38", "39", "40",
    "41", "42", "43", "44", "45", "46", "47", "48", "49", "50",
    "51", "52", "53", "54", "55", "56", "57", "58", "59", NULL};

  hourChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (*container,
								hourChoices,
								hours));
  {
    Widget labelWidget = xxX::createXtWidget (":", labelWidgetClass,
      container->widget(), labelArgs, 3);
    helpLabel2 = xxX::wrap (labelWidget);
  }
  minuteChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (
								 *container,
								 minuteChoices,
								 minutes));
  {
    Widget labelWidget = xxX::createXtWidget ("  ", labelWidgetClass,
      container->widget(), labelArgs, 3);
    spaceLabel = xxX::wrap (labelWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Go", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxStepCallback,
     (XtPointer)this);
    goButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Cancel", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxStepCancelCallback,
      (XtPointer)this);
    cancelButton = xxX::wrap (buttonWidget);
  }

  realize();
}

// Cleanup2006 Done
