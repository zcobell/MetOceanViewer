// $Id: xxTextMode.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  xxTextMode  Raw/medium/plain modes, in a window.

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
#include "xxRareModeSavePrompts.hh"
#include "xxReconfigurable.hh"
#include "xxFilename.hh"
#include "xxTextMode.hh"


// How many lines by default in text, raw, and medium rare mode windows.
static const unsigned origTextModeLines (10U);


void xxTextMode::help() {
  Dstr helpstring;

  if (_mode == Mode::plain)
    helpstring = "\
XTide Plain Mode Window\n\
\n\
Use the forward and backward buttons to scroll the tide predictions\n\
forward or backward in time.  You can resize the window vertically\n\
to see more predictions at once.\n\
\n\
The options menu supplies the following commands:\n\
\n\
  Save:  Use this to write the tide predictions to a text file.\n\
\n\
  Set Mark:  This is used to set the 'mark level' for tide predictions.\n\
  The prediction window will show the times when the tide level crosses\n\
  the mark.\n\
\n\
  Convert ft<->m:  Convert feet to meters or vice-versa.\n\
\n\
  Set Time:  Go to a different point in time.  Major adjustments that\n\
  cannot be made with the forward and backward buttons can be made with\n\
  Set Time.\n\
\n\
  New Graph Window:  Open a window with a tide graph for the current\n\
  location and time.\n\
\n\
  New Plain Mode Window:  Duplicates the existing text window.\n\
\n\
  New Raw Mode Window:  Open a window with a text listing of unprocessed\n\
  numerical time stamps and tide levels for the current location and time.\n\
\n\
  New Medium Rare Mode Window:  Open a window with a text listing of\n\
  processed timestamps and unprocessed numerical tide levels for the\n\
  current location and time.\n\
\n\
  New Clock Window:  Open a window with a tide clock for the current\n\
  location.\n\
\n\
  New Location Chooser:  Open new globe and location list windows to allow\n\
  selecting a new location.";
  else {

  if (_mode == Mode::mediumRare)
    helpstring = "XTide Medium Rare Mode Window\n";
  else
    helpstring = "XTide Raw Mode Window\n";
  helpstring += "\n\
Use the forward and backward buttons to scroll the tide predictions\n\
forward or backward in time.  You can resize the window vertically\n\
to see more predictions at once.\n\
\n\
The options menu supplies the following commands:\n\
\n\
  Save:  Use this to send output to a text file.  In this mode you are\n\
  given the opportunity to generate output for a different time interval\n\
  than is displayed on the screen.\n\
\n\
  Convert ft<->m:  Convert feet to meters or vice-versa.\n\
\n\
  Set Time:  Go to a different point in time.  Major adjustments that\n\
  cannot be made with the forward and backward buttons can be made with\n\
  Set Time.\n\
\n\
  Set Step:  Change the time increment of the output.\n\
\n\
  New Graph Window:  Open a window with a tide graph for the current\n\
  location and time.\n\
\n\
  New Plain Mode Window:  Open a window with a text listing of tide\n\
  predictions for the current location and time.\n\
\n";

  if (_mode == Mode::mediumRare)
    helpstring += "\
  New Raw Mode Window:  Open a window with a text listing of unprocessed\n\
  numerical time stamps and tide levels for the current location and time.\n\
\n\
  New Medium Rare Mode Window:  Duplicates the existing medium rare mode\n\
  window.\n";
  else
    helpstring += "\
  New Raw Mode Window:  Duplicates the existing raw mode window.\n\
\n\
  New Medium Rare Mode Window:  Open a window with a text listing of\n\
  processed timestamps and unprocessed numerical tide levels for the\n\
  current location and time.\n";

  helpstring += "\n\
  New Clock Window:  Open a window with a tide clock for the current\n\
  location.\n\
\n\
  About This Station:  Show station metadata.\n\
\n\
  About XTide:  Show XTide version and GPL.\n\
\n\
  New Location Chooser:  Open new globe and location list windows to allow\n\
  selecting a new location.\n\
\n\
  Control Panel:  Advanced XTide configuration.";
  }
  (void) xxroot->newHelpBox (helpstring);
}


static void mouseButtonCallback (Widget w unusedParameter,
                                 XtPointer client_data,
                                 XEvent *event,
                                 Boolean *continue_dispatch unusedParameter) {
  assert (client_data);
  ((xxTextMode*)client_data)->mouseButton ((XButtonEvent*)event);
}


void xxTextMode::mouseButton (const XButtonEvent *xbe) {
  assert (xbe);
  switch (xbe->button) {
  case Button4:
    backward();
    break;
  case Button5:
    forward();
  }
}


static void xxTextModeforwardCallback (Widget w unusedParameter,
                                       XtPointer client_data,
                                       XtPointer call_data unusedParameter) {
  ((xxTextMode *)client_data)->forward();
}


void xxTextMode::forward() {
  TideEventsIterator it = organizer.upper_bound (t);
  if (it == organizer.end()) {
    // This should only happen here if lines == 1; see draw().
    extendRange (Station::forward);
    forward();
  } else {
    t = it->second.eventTime;
    draw();
  }
}


static void xxTextModebackwardCallback (Widget w unusedParameter,
                                        XtPointer client_data,
                                        XtPointer call_data unusedParameter) {
  ((xxTextMode *)client_data)->backward();
}


void xxTextMode::backward() {
  TideEventsIterator it = organizer.lower_bound (t);
  assert (it != organizer.end());
  if (it == organizer.begin()) {
    extendRange (Station::backward);
    backward();
  } else {
    --it;
    t = it->second.eventTime;
    draw();
  }
}


static void xxTextModeResizeHandler (
                                  Widget w unusedParameter,
                                  XtPointer client_data,
                                  XEvent *event,
                                  Boolean *continue_dispatch unusedParameter) {
  assert (event);
  if (event->type == ConfigureNotify) {
    assert (client_data);
    ((xxTextMode *)client_data)->resize (((XConfigureEvent *)event)->height);
  }
}


void xxTextMode::resize (Dimension newHeight) {
  if (newHeight != height) {
    height = newHeight;
    // Extremely kludgy guesstimate of how many lines we should have now.
    int t = (int)((double)(newHeight - (origHeight - origLabelHeight)) *
                  (double)origTextModeLines / (double)origLabelHeight);
    lines = std::max (t, 1);
    redraw();
  }
}


void xxTextMode::save () {
  if (_mode == Mode::plain)
    (void) new xxFilename (*popup, *this, true);
  else
    (void) new xxRareModeSavePrompts (*popup,
				      *this,
				      "tides.txt",
				      t,
				      lastLineTime+_station->step,
				      _station->timezone);
}


void xxTextMode::save (const Dstr &filename, Format::Format form) {
  FILE *fp;
  assert (form == Format::text);
  if ((fp = fopen (filename.aschar(), "w"))) {
    Dstr text;
    _station->plainMode (text,
                         t - Global::eventSafetyMargin,
                         lastLineTime + Global::eventSafetyMargin,
                         Format::text);
    fprintf (fp, "%s", text.aschar());
    fclose (fp);
  } else
    Global::cantOpenFile (filename, Error::nonfatal);
}


void xxTextMode::save (const Dstr &filename,
		       Timestamp startTime,
		       Timestamp endTime) {
  FILE *fp;
  if ((fp = fopen (filename.aschar(), "w"))) {
    Dstr text;
    _station->rareModes (text, startTime, endTime, _mode, Format::text);
    fprintf (fp, "%s", text.aschar());
    fclose (fp);
  } else
    Global::cantOpenFile (filename, Error::nonfatal);
}


xxTextMode::xxTextMode (const xxWidget &shell,
			Station *station,
			Mode::Mode mode):
  xxPredictionWindow (shell, station, (time_t)time(NULL), formContainer),
  _mode(mode) {
  construct();
}


xxTextMode::xxTextMode (const xxWidget &shell,
			Station *station,
			Mode::Mode mode,
			Timestamp startTime):
  xxPredictionWindow (shell, station, startTime, formContainer),
  _mode(mode) {
  construct();
}


void xxTextMode::construct () {
  lines = origTextModeLines;
  Dstr title (_station->name);
  switch (_mode) {
  case Mode::raw:
    title += " (Raw)";
    break;
  case Mode::mediumRare:
    title += " (Medium Rare)";
    break;
  case Mode::plain:
    title += " (Text)";
    break;
  default:
    assert (false);
  }
  setTitle (title);
  XtAddEventHandler (popup->widget(), StructureNotifyMask, False,
		     xxTextModeResizeHandler, (XtPointer)this);
  XtAddEventHandler (popup->widget(), ButtonPressMask, False,
                     mouseButtonCallback, (XtPointer)this);

  {
    Arg labelArgs[6] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainTop},
      {(char*)XtNbottom, (XtArgVal)XawChainTop}
    };
    Widget labelWidget = xxX::createXtWidget (_station->name.aschar(),
      labelWidgetClass, container->widget(), labelArgs, 6);
    nameLabel = xxX::wrap (labelWidget);
  }{
    Arg labelArgs[9] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)nameLabel->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainRight},
      {(char*)XtNtop, (XtArgVal)XawChainTop},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom},
      {XtNjustify, (XtArgVal)XtJustifyLeft},
      {fontArgName, xxX::monoFontArgValue}
    };
    Widget labelWidget = xxX::createXtWidget ("",
      labelWidgetClass, container->widget(), labelArgs, 9);
    label = xxX::wrap (labelWidget);
  }{
    Arg buttonArgs[7] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)label->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonwidget = xxX::createXtWidget ("Backward",
      repeaterWidgetClass, container->widget(), buttonArgs, 7);
    XtAddCallback (buttonwidget, XtNcallback, xxTextModebackwardCallback,
     (XtPointer)this);
    backwardButton = xxX::wrap (buttonwidget);
  }{
    Arg buttonArgs[8] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)label->widget()},
      {(char*)XtNfromHoriz, (XtArgVal)backwardButton->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonwidget = xxX::createXtWidget ("Forward",
      repeaterWidgetClass, container->widget(), buttonArgs, 8);
    XtAddCallback (buttonwidget, XtNcallback, xxTextModeforwardCallback,
     (XtPointer)this);
    forwardButton = xxX::wrap (buttonwidget);
  }

  addNormalButtons (label->widget(), forwardButton->widget());

  redraw();
  realize();
  widthNoSmaller();

  {
    Arg args[1] = {{XtNheight, (XtArgVal)(&origHeight)}};
    XtGetValues (container->widget(), args, 1);
    height = origHeight;
  }{
    Dimension internalHeight;
    Arg args[2] = {
      {XtNheight, (XtArgVal)(&origLabelHeight)},
      {XtNinternalHeight, (XtArgVal)(&internalHeight)}
    };
    XtGetValues (label->widget(), args, 2);
    origLabelHeight -= internalHeight * 2;
  }
}


void xxTextMode::redraw() {
  organizer.clear();
  if (_mode == Mode::plain) {
    // Make sure we get at least one event so that extendRange can
    // work if necessary.
    for (Interval delta (standardDeltaPlain());
	 organizer.empty();
	 delta *= 2U)
      _station->predictTideEvents (t - Global::eventSafetyMargin,
				   t + delta,
				   organizer);

    // Here was an interesting bug:  The safety interval above
    // suffices to recapture tide events that shift earlier (by up to
    // eventPrecision), but if we don't reset t to the *new* time of
    // the first event, draw() will skip the first event so it will
    // appear as if we missed it anyway.  OTOH if we decrement t above
    // it may continue to creep backward indefinitely.
    t = organizer.begin()->second.eventTime;

  } else
    _station->predictRawEvents (t,
				t+((_station->step)*standardDeltaRare()),
				organizer);

  // Was having major trouble getting the label widget to resize itself
  // reliably.  This seems to help.
  {
    int heightGuess (height - (origHeight - origLabelHeight));
    if (heightGuess < 1)
      heightGuess = 1;
    Arg labelArgs[1] =  {{XtNheight, (XtArgVal)heightGuess}};
    XtSetValues (label->widget(), labelArgs, 1);
  }

  draw();
}


void xxTextMode::draw () {
  TideEventsIterator it (organizer.lower_bound (t));
  Dstr buf;
  if (it != organizer.end())
    t = it->second.eventTime;
  for (unsigned a=0; a<lines; ++a) {
    if (it == organizer.end()) {
      extendRange (Station::forward);
      draw();
      return;
    } else {
      Dstr line;
      it->second.print (line, _mode, Format::text, *_station);
      if (!buf.isNull())
        buf += '\n';
      buf += line;
      lastLineTime = it->second.eventTime;
      ++it;
    }
  }
  Arg labelArgs[1] =  {{XtNlabel, (XtArgVal)buf.aschar()}};
  XtSetValues (label->widget(), labelArgs, 1);
}


xxTextMode::~xxTextMode() {
  unrealize();
}


void xxTextMode::globalRedraw() {
  xxPredictionWindow::globalRedraw();
  redraw();
  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (backwardButton.get());
  XtSetValues (backwardButton->widget(), buttonArgs, 2);
  assert (forwardButton.get());
  XtSetValues (forwardButton->widget(), buttonArgs, 2);
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (label.get());
  XtSetValues (label->widget(), args, 2);
  assert (nameLabel.get());
  XtSetValues (nameLabel->widget(), args, 2);
}


const bool xxTextMode::isRare() const {
  return (_mode != Mode::plain);
}


void xxTextMode::extendRange (Station::Direction direction) {
  if (_mode == Mode::plain) {
    TideEventsOrganizer::size_type size (organizer.size());
    for (Interval delta (standardDeltaPlain());
	 organizer.size() == size;
	 delta *= 2U)
      _station->extendRange (organizer, direction, delta);
  } else
    _station->extendRange (organizer, direction, standardDeltaRare());
}


const Interval xxTextMode::standardDeltaPlain() {
  return Global::halfCycle * std::max(lines,10U);
}


const unsigned xxTextMode::standardDeltaRare() {
  return std::max(lines,10U) * 2U;
}

// Cleanup2006 Done
