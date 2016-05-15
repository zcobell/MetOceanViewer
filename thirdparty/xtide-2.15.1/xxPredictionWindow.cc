// $Id: xxPredictionWindow.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  xxPredictionWindow  Abstract class for all tide-predicting windows.

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
#include "xxSimplePrompt.hh"
#include "xxMarkLevel.hh"
#include "xxAspect.hh"
#include "xxMultiChoice.hh"
#include "xxStep.hh"
#include "xxTimestampDialog.hh"
#include "xxTimestamp.hh"


xxPredictionWindow::xxPredictionWindow (const xxWidget &shell,
					Station *station,
					Timestamp startTime,
					ContainerType containerType):
  xxWindow (shell, containerType),
  _station(station),
  t(startTime) {
  assert (_station.get());
}


xxPredictionWindow::~xxPredictionWindow() {
  unrealize();
}


static void dismissCallback (Widget w unusedParameter,
			     XtPointer client_data,
			     XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxPredictionWindow *)client_data)->dismiss();
}


static void helpCallback (Widget w unusedParameter,
			  XtPointer client_data,
			  XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->help();
}


static void saveCallback (Widget w unusedParameter,
			  XtPointer client_data,
			  XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->save();
}


static void markCallback (Widget w unusedParameter,
			  XtPointer client_data,
			  XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->mark();
}


static void unitsCallback (Widget w unusedParameter,
			   XtPointer client_data,
			   XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->units();
}


void xxPredictionWindow::units() {
  Units::PredictionUnits units (_station->predictUnits());
  switch (units) {
  case Units::feet:
    units = Units::meters;
    break;
  case Units::meters:
    units = Units::feet;
    break;
  default:
    assert (false);
  }
  _station->setUnits (units);
  redraw();
}


static void timestampCallback (Widget w unusedParameter,
			       XtPointer client_data,
			       XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->timestamp();
}


static void aspectCallback (Widget w unusedParameter,
			    XtPointer client_data,
			    XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->aspect();
}


static void stepCallback (Widget w unusedParameter,
			  XtPointer client_data,
			  XtPointer call_data unusedParameter) {
  assert (client_data);
  ((xxPredictionWindow*)client_data)->step();
}


static void xxPredictionWindowGraphCallback (
                                         Widget w unusedParameter,
				         XtPointer client_data,
					 XtPointer call_data unusedParameter) {
  assert (client_data);
  xxPredictionWindow *d ((xxPredictionWindow*)client_data);
  xxroot->newGraph (d->station()->clone(), d->startTime());
}


static void xxPredictionWindowPlainCallback (
                                         Widget w unusedParameter,
				         XtPointer client_data,
					 XtPointer call_data unusedParameter) {
  assert (client_data);
  xxPredictionWindow *d ((xxPredictionWindow*)client_data);
  xxroot->newPlain (d->station()->clone(), d->startTime());
}


static void xxPredictionWindowRawCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  xxPredictionWindow *d ((xxPredictionWindow*)client_data);
  xxroot->newRaw (d->station()->clone(), d->startTime());
}


static void xxPredictionWindowMediumRareCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  xxPredictionWindow *d ((xxPredictionWindow*)client_data);
  xxroot->newMediumRare (d->station()->clone(), d->startTime());
}


static void xxPredictionWindowClockCallback (
                                         Widget w unusedParameter,
				         XtPointer client_data,
					 XtPointer call_data unusedParameter) {
  assert (client_data);
  xxPredictionWindow *d ((xxPredictionWindow*)client_data);
  xxroot->newClock (d->station()->clone());
}


static void xxPredictionWindowAboutSCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data,
                                         XtPointer call_data unusedParameter) {
  assert (client_data);
  xxPredictionWindow *d ((xxPredictionWindow*)client_data);
  xxroot->newAbout (d->station());
}


static void xxPredictionWindowAboutXCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data unusedParameter,
                                         XtPointer call_data unusedParameter) {
  xxroot->newAboutXTide();
}


static void xxPredictionWindowChooserCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data unusedParameter,
                                         XtPointer call_data unusedParameter) {
  xxroot->newChooser();
}


static void xxPredictionWindowControlPanelCallback (
                                         Widget w unusedParameter,
                                         XtPointer client_data unusedParameter,
                                         XtPointer call_data unusedParameter) {
  xxroot->realize();
}


void xxPredictionWindow::mark() {
  PredictionValue initMark;
  if (_station->markLevel.isNull())
    initMark = PredictionValue (_station->predictUnits(), -0.25);
  else
    initMark = _station->markLevel;
  (void) new xxMarkLevel (*popup, *this, initMark);
}


void xxPredictionWindow::mark (NullablePredictionValue newMarkLevel) {
  _station->markLevel = newMarkLevel;
  redraw();
}


void xxPredictionWindow::timestamp() {
  assert (!isClock());
  (void) new xxTimestamp (*popup, *this, t, _station->timezone);
}


void xxPredictionWindow::timestamp (Timestamp newTimestamp) {
  t = newTimestamp;
  redraw();
}


void xxPredictionWindow::aspect() {
  (void) new xxAspect (*popup, *this, _station->aspect);
}


void xxPredictionWindow::aspect (double newAspect) {
  assert (newAspect > 0.0);
  _station->aspect = newAspect;
  redraw();
}


void xxPredictionWindow::step() {
  (void) new xxStep (*popup, *this, _station->step);
}


void xxPredictionWindow::step (Interval newStep) {
  assert (newStep > Global::zeroInterval);
  _station->step = newStep;
  redraw();
}


void xxPredictionWindow::addNormalButtons (Widget northWidget,
					   Widget westWidget) {
  assert (popup.get());
  assert (container.get());

  Arg boxButtonArgs[5] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {(char*)"menuName", (XtArgVal)"optionsMenu"}
  };
  Arg formButtonArgs[11] =  {
    {(char*)XtNfromHoriz, (XtArgVal)westWidget},
    {(char*)XtNfromVert, (XtArgVal)northWidget},
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {(char*)XtNleft, (XtArgVal)XawChainLeft},
    {(char*)XtNright, (XtArgVal)XawChainLeft},
    {(char*)XtNtop, (XtArgVal)XawChainBottom},
    {(char*)XtNbottom, (XtArgVal)XawChainBottom},
    {(char*)"menuName", (XtArgVal)"optionsMenu"}
  };

  // To avoid having to repeat all those args several times,
  // XtNfromHoriz gets overwritten and the last one is only used for
  // the options button (by diddling the length passed to
  // xxX::createXtWidget).

  Arg *buttonArgs;
  unsigned numButtonArgs;
  if (_containerType == boxContainer) {
    buttonArgs = boxButtonArgs;
    numButtonArgs = 4;
  } else {
    buttonArgs = formButtonArgs;
    numButtonArgs = 10;
  }

  // The options menu.
  {
    Widget optionsButtonWidget = xxX::createXtWidget ("Options",
      menuButtonWidgetClass, container->widget(), buttonArgs, numButtonArgs+1);
    optionsButton = xxX::wrap (optionsButtonWidget);
    formButtonArgs[0].value = (XtArgVal)optionsButtonWidget;
  }{
    Widget menuShell = XtCreatePopupShell ("optionsMenu",
      simpleMenuWidgetClass, optionsButton->widget(), buttonArgs,
      numButtonArgs);
    optionsMenu = xxX::wrap (menuShell);
    xxX::fixBorder (menuShell);
  }
  // Buttons on the options menu.
  {
    Widget buttonWidget = xxX::createXtWidget ("Save", smeBSBObjectClass,
      optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, saveCallback, (XtPointer)this);
    saveButton = xxX::wrap (buttonWidget);
  }
  if (!isRare()) {
    Widget buttonWidget = xxX::createXtWidget ("Set Mark", smeBSBObjectClass,
      optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, markCallback, (XtPointer)this);
    markButton = xxX::wrap (buttonWidget);
  }
  if (!(_station->isCurrent)) {
    Widget buttonWidget = xxX::createXtWidget ("Convert ft<->m",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, unitsCallback, (XtPointer)this);
    unitsButton = xxX::wrap (buttonWidget);
  }
  if (!isClock()) {
    Widget buttonWidget = xxX::createXtWidget ("Set Time", smeBSBObjectClass,
      optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, timestampCallback,
		   (XtPointer)this);
    timestampButton = xxX::wrap (buttonWidget);
  }
  if (isGraph()) {
    Widget buttonWidget = xxX::createXtWidget ("Set Aspect",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, aspectCallback, (XtPointer)this);
    aspectButton = xxX::wrap (buttonWidget);
  }
  if (isRare()) {
    Widget buttonWidget = xxX::createXtWidget ("Set Step", smeBSBObjectClass,
      optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, stepCallback, (XtPointer)this);
    stepButton = xxX::wrap (buttonWidget);
  }
  {
    Widget buttonWidget = xxX::createXtWidget ("New Graph Window",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxPredictionWindowGraphCallback,
		   (XtPointer)this);
    graphButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("New Plain Mode Window",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxPredictionWindowPlainCallback,
		   (XtPointer)this);
    plainButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("New Raw Mode Window",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxPredictionWindowRawCallback,
		   (XtPointer)this);
    rawButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget (
      "New Medium Rare Mode Window", smeBSBObjectClass, optionsMenu->widget(),
      boxButtonArgs, 4);
    XtAddCallback (buttonWidget,
		   XtNcallback,
		   xxPredictionWindowMediumRareCallback,
		   (XtPointer)this);
    mediumRareButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("New Clock Window",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxPredictionWindowClockCallback,
		   (XtPointer)this);
    clockButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("About This Station",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxPredictionWindowAboutSCallback,
		   (XtPointer)this);
    aboutStationButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("About XTide",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxPredictionWindowAboutXCallback,
		   (XtPointer)this);
    aboutXTideButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("New Location Chooser",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget,
		   XtNcallback,
		   xxPredictionWindowChooserCallback,
		   (XtPointer)this);
    chooserButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Control Panel",
      smeBSBObjectClass, optionsMenu->widget(), boxButtonArgs, 4);
    XtAddCallback (buttonWidget,
		   XtNcallback,
		   xxPredictionWindowControlPanelCallback,
		   (XtPointer)this);
    rootButton = xxX::wrap (buttonWidget);
  }
  // Top level buttons.
  {
    Widget buttonWidget = xxX::createXtWidget ("Dismiss", commandWidgetClass,
      container->widget(), buttonArgs, numButtonArgs);
    XtAddCallback (buttonWidget, XtNcallback, dismissCallback,
     (XtPointer)this);
    dismissButton = xxX::wrap (buttonWidget);
    formButtonArgs[0].value = (XtArgVal)buttonWidget;
  }{
    Widget buttonWidget = xxX::createXtWidget ("?", commandWidgetClass,
      container->widget(), buttonArgs, numButtonArgs);
    XtAddCallback (buttonWidget, XtNcallback, helpCallback,
      (XtPointer)this);
    helpButton = xxX::wrap (buttonWidget);
  }
}


const bool xxPredictionWindow::isGraph() const {
  return false;
}


const bool xxPredictionWindow::isClock() const {
  return false;
}


const bool xxPredictionWindow::isRare() const {
  return false;
}


void xxPredictionWindow::globalRedraw() {
  xxWindow::globalRedraw();

  // Applying and unapplying the Infer setting necessitates reloading
  // of stations.
  _station = std::auto_ptr<Station>(_station->reload());

  redraw();
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (optionsButton.get());
  XtSetValues (optionsButton->widget(), buttonArgs, 2);
  assert (optionsMenu.get());
  XtSetValues (optionsMenu->widget(), buttonArgs, 2);
  assert (saveButton.get());
  XtSetValues (saveButton->widget(), buttonArgs, 2);
  if (markButton.get())
    XtSetValues (markButton->widget(), buttonArgs, 2);
  if (unitsButton.get())
    XtSetValues (unitsButton->widget(), buttonArgs, 2);
  if (timestampButton.get())
    XtSetValues (timestampButton->widget(), buttonArgs, 2);
  if (aspectButton.get())
    XtSetValues (aspectButton->widget(), buttonArgs, 2);
  if (stepButton.get())
    XtSetValues (stepButton->widget(), buttonArgs, 2);
  assert (graphButton.get());
  XtSetValues (graphButton->widget(), buttonArgs, 2);
  assert (plainButton.get());
  XtSetValues (plainButton->widget(), buttonArgs, 2);
  assert (rawButton.get());
  XtSetValues (rawButton->widget(), buttonArgs, 2);
  assert (mediumRareButton.get());
  XtSetValues (mediumRareButton->widget(), buttonArgs, 2);
  assert (clockButton.get());
  XtSetValues (clockButton->widget(), buttonArgs, 2);
  assert (aboutStationButton.get());
  XtSetValues (aboutStationButton->widget(), buttonArgs, 2);
  assert (aboutXTideButton.get());
  XtSetValues (aboutXTideButton->widget(), buttonArgs, 2);
  assert (chooserButton.get());
  XtSetValues (chooserButton->widget(), buttonArgs, 2);
  assert (rootButton.get());
  XtSetValues (rootButton->widget(), buttonArgs, 2);
  assert (dismissButton.get());
  XtSetValues (dismissButton->widget(), buttonArgs, 2);
  assert (helpButton.get());
  XtSetValues (helpButton->widget(), buttonArgs, 2);
}


const Timestamp xxPredictionWindow::startTime() const {
  return t;
}


Station * const xxPredictionWindow::station() const {
  assert (_station.get());
  return _station.get();
}
