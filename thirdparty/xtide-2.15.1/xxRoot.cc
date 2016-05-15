// $Id: xxRoot.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  xxRoot  XTide "root" window (control panel, top-level logic)

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
#include "xxGraphMode.hh"
#include "xxDisclaimer.hh"
#include "xxReconfigurable.hh"
#include "xxLocationList.hh"
#include "xxGlobe.hh"
#include "xxMap.hh"
#include "xxHorizDialog.hh"
#include "xxUnsignedChooser.hh"
#include "xxMultiChoice.hh"
#include "xxErrorBox.hh"
#include "xxHelpBox.hh"
#include "xxTextMode.hh"


static const unsigned controlPanelInitialHeight (900U);

// Completely wild guess of how much room is needed by the window dressing on
// an xxHelpBox (pixels).
static const unsigned windowDressingWidth (32U);


static void errorCallback (const Dstr &errorMessage, Error::ErrType fatality) {
  xxroot->newErrorBox (errorMessage, fatality);
}


static void dismissCallback (Widget w unusedParameter,
                             XtPointer client_data unusedParameter,
                             XtPointer call_data unusedParameter) {
  xxroot->unrealize();
}


static void applyCallback (Widget w unusedParameter,
                           XtPointer client_data unusedParameter,
                           XtPointer call_data unusedParameter) {
  xxroot->apply (xxRoot::justApply);
}


static void changeUserDefaults (const Settings &newUserDefaults) {
  Global::settings = Settings();
  Global::settings.applyXResources();
  Global::settings.apply (newUserDefaults);
  Global::settings.applyCommandLine();
  Global::settings.fixUpDeprecatedSettings();
  xxX::installColors();
}


void xxRoot::apply (ApplyProtocol protocol) {

  // Build new settings for ~/.xtide.xml.
  Settings newUserDefaults;
  newUserDefaults.nullify();

  // There are several ways to implement this, all of which involve
  // messy nested logic because the association between dialog classes
  // and configurable interpretations is not simple.  This way at
  // least has no duplicated code and at most two redundant
  // operations (the failed dynamic casts).

  for (BetterMap<const Dstr, xxRedrawable*>::iterator it = dialogs.begin();
       it != dialogs.end();
       ++it) {
    xxRedrawable *redrawable (it->second);
    assert (redrawable);
    assert (newUserDefaults.count(it->first) == 1);
    Configurable &configurable (newUserDefaults[it->first]);
    assert (!configurable.caption.isNull());
    assert (configurable.kind == Configurable::settingKind);

    xxMultiChoice *multiChoice (dynamic_cast<xxMultiChoice*>(redrawable));
    if (multiChoice) {
      const unsigned choice (multiChoice->choice());
      switch (configurable.interpretation) {
      case Configurable::booleanInterp:
	switch (choice) {
	case 0:
	  configurable.isNull = false;
	  configurable.c = 'y';
	  break;
	case 1:
	  configurable.isNull = false;
	  configurable.c = 'n';
	  break;
	case 2:
	  break;
	default:
	  assert (false);
	}
	break;
      case Configurable::glDoubleInterp:
        assert (choice < 14);
	switch (choice) {
	case 12:
	  configurable.isNull = false;
	  configurable.d = 360.0;
	  break;
	case 13:
	  break;
	default:
	  configurable.isNull = false;
	  configurable.d = (double)choice * 30.0 - 180.0;
	  break;
	}
	break;
      case Configurable::unitInterp:
	switch (choice) {
	case 0:
	  configurable.isNull = false;
	  configurable.s = "ft";
	  break;
	case 1:
	  configurable.isNull = false;
	  configurable.s = "m";
	  break;
	case 2:
	  configurable.isNull = false;
	  configurable.s = "x";
	  break;
	case 3:
	  break;
	default:
	  assert (false);
	}
	break;
      case Configurable::gsInterp:
	switch (choice) {
	case 0:
	  configurable.isNull = false;
	  configurable.c = 'd';
	  break;
	case 1:
	  configurable.isNull = false;
	  configurable.c = 'l';
	  break;
	case 2:
	  configurable.isNull = false;
	  configurable.c = 's';
	  break;
	case 3:
	  break;
	default:
	  assert (false);
	}
	break;
      default:
        assert (false);
      }

    } else {
      xxUnsignedChooser *unsignedChooser (
                                 dynamic_cast<xxUnsignedChooser*>(redrawable));
      if (unsignedChooser) {
        const unsigned choice (unsignedChooser->choice());
        assert (configurable.interpretation == Configurable::posIntInterp);
	if (choice != UINT_MAX) {
          assert (choice > 0);
	  configurable.isNull = false;
	  configurable.u = choice;
	}
      } else {
        xxHorizDialog *horizDialog (dynamic_cast<xxHorizDialog*>(redrawable));
        assert (horizDialog);
        const Dstr value (horizDialog->val());
	switch (configurable.interpretation) {
	case Configurable::posDoubleInterp:
	case Configurable::nonnegativeDoubleInterp:
	case Configurable::opacityDoubleInterp:
	  {
	    Global::GetDoubleReturn gdr (Global::getDouble (
                                                   value,
                                                   configurable.interpretation,
						   configurable.d));
            switch (gdr) {
            case Global::inputNotOK:
              return;
            case Global::emptyInput:
              break;
            case Global::inputOK:
              configurable.isNull = false;
              break;
            default:
              assert (false);
            }
	  }
	  break;
        case Configurable::textInterp:
	  if (value.length()) {
	    configurable.isNull = false;
	    configurable.s = value;
	  }
	  break;
	case Configurable::colorInterp:
	  if (value.length()) {
	    // Sanity check color
	    uint8_t r, g, b;
	    if (!(Colors::parseColor (value, r, g, b, Error::nonfatal)))
	      return;
	    configurable.isNull = false;
	    configurable.s = value;
	  }
	  break;
	case Configurable::timeFormatInterp:
	  if (value.length()) {
	    // Sanity check strftime format string
	    if (value.strchr('"') != -1 ||
                value.strchr('>') != -1 ||
		value.strchr('<') != -1) {
	      Global::barf (Error::XMLPARSE,
                            "Please don't use nasty characters like \", >, and < in your date/time formats.",
                            Error::nonfatal);
	      return;
	    }
	    configurable.isNull = false;
	    configurable.s = value;
	  }
	  break;
	case Configurable::eventMaskInterp:
	  if (value.length()) {
	    if (!Global::isValidEventMask (value)) {
	      Global::barf (Error::BAD_EVENTMASK, Error::nonfatal);
	      return;
	    }
	    configurable.isNull = false;
	    configurable.s = value;
	  }
	  break;
	default:
	  assert (false);
	}
      }
    }
  }

  changeUserDefaults (newUserDefaults);
  globalRedraw();

  // Provided that the world did not explode as a result of applying those
  // settings, only then might they be saved.
  if (protocol == applyAndSave)
    newUserDefaults.save();
}


static void saveCallback (Widget w unusedParameter,
                          XtPointer client_data unusedParameter,
                          XtPointer call_data unusedParameter) {
  xxroot->apply (xxRoot::applyAndSave);
}


static void helpCallback (Widget w unusedParameter,
                          XtPointer client_data unusedParameter,
                          XtPointer call_data unusedParameter) {
  Dstr helpstring ("\
XTide Control Panel\n\
\n\
The Control Panel is used to change global XTide settings.  These settings\n\
take precedence over compiled-in defaults and X application defaults, but\n\
they are overridden by settings made on the command line.  Therefore, any\n\
settings that you make in the Control Panel will not have any visible effect\n\
if you have also made these settings on the command line.\n\
\n\
Some settings have dialog boxes for you to type in; others have pull-down\n\
menus or '+' and '-' controls.  In each case, there is a way to leave the\n\
field blank if you want to keep the inherited settings.  Either delete all\n\
text in the dialog box, or choose the \"(blank)\" option on the pull-down\n\
menu, or lay on the '-' button until the field reads \"(blank)\".\n\
\n\
The settings identified as \"default\" settings (widths, heights, and\n\
aspect ratio) will not affect existing windows.  Widths and heights will\n\
affect all new windows; aspect ratio will only affect new windows that\n\
are created from the location chooser.  (This is because the aspect is\n\
preserved from one tide window to any new windows that it spawns.)\n\
\n\
Use the Apply button to apply the new settings to the current XTide session\n\
without making them permanent.  Use Save to make them permanent.\n\
\n\
If necessary, resize the Control Panel to keep all of the settings visible.\n\
\n\
About colors:  When entering colors, use either standard X11 color names\n\
or 24-bit hex specs of the form rgb:hh/hh/hh.  Do not use more or less bits;\n\
it will not work.\n\
\n\
About the event mask:  Events to suppress (p = phase of moon, S =\n\
sunrise, s = sunset, M = moonrise, m = moonset), or x to suppress\n\
none.  E.g, to suppress all sun and moon events, set eventmask to the\n\
value pSsMm.\n\
\n\
About date/time formats:  Please refer to the man page for strftime to see\n\
how these formats work.  Here is how to get XTide to use 24-hour time instead\n\
of AM/PM:\n\
   Hour format  %H\n\
   Time format  %H:%M %Z");
  (void) xxroot->newHelpBox (helpstring);
}


// This function is passed to Settings::applyXResources.

static XrmDatabase database;
static const bool getResource (const Dstr &name, Dstr &val_out) {
  Dstr n1 ("xtide*"), n2 ("XTide*");
  n1 += name;
  n2 += name;
  XrmValue xv;
  char *str_type[20]; // Waste.
  if (XrmGetResource (database, n1.aschar(), n2.aschar(), str_type, &xv)) {
    val_out = (char *)xv.addr;
    return true;
  }
  return false;
}


xxRoot::xxRoot (int argc, char **argv):
  xxWindow (argc, argv),
  popupCount(0) {

  setTitle ("Control Panel");

  // Switches recognized by X11 are removed from the command line by
  // the xxWidget constructor.
  database = XtScreenDatabase (xxX::screen);
  Global::settings.applyXResources (getResource);
  Global::settings.applyUserDefaults();
  Global::settings.applyCommandLine (argc, argv);
  Global::settings.fixUpDeprecatedSettings();
  xxX::connectPostSettingsHook (popup->widget());

  Global::setErrorCallback (&errorCallback);

  {
    // Can't do this until after the settings have been processed.
    Arg args[2] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
    };
    XtSetValues (popup->widget(), args, 2);
  }{
    // Slightly different than what xxWindow would provide.
    Arg formArgs[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNvSpace, (XtArgVal)0}
    };
    Widget formWidget = xxX::createXtWidget ("", formWidgetClass,
      popup->widget(), formArgs, 3);
    container = xxX::wrap (formWidget);
  }{
    Arg labelArgs[6] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainTop},
      {(char*)XtNbottom, (XtArgVal)XawChainTop}
    };
    Widget labelWidget = xxX::createXtWidget (
      "-------------- XTide Control Panel --------------",
      labelWidgetClass, container->widget(), labelArgs, 6);
    label = new xxWidget (labelWidget);
  }{
    Arg vpArgs[10] =  {
      {XtNheight, (XtArgVal)controlPanelInitialHeight},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNallowVert, (XtArgVal)1},
      {XtNforceBars, (XtArgVal)1},
      {(char*)XtNfromVert, (XtArgVal)label->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainRight},
      {(char*)XtNtop, (XtArgVal)XawChainTop},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };

    Widget viewportWidget = xxX::createXtWidget ("", viewportWidgetClass,
      container->widget(), vpArgs, 10);
    viewport = new xxWidget (viewportWidget);
    xxX::fixBorder (viewportWidget);

    require (scrollbarWidget = XtNameToWidget (viewportWidget, "vertical"));
    Arg sbArgs[2] = {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
    };
    XtSetValues (scrollbarWidget, sbArgs, 2);
    setRudeness (scrollbarWidget);
  }{
    Arg boxArgs[4] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNvSpace, (XtArgVal)0},
      {XtNorientation, (XtArgVal)XtorientVertical}
    };
    Widget boxWidget = xxX::createXtWidget ("", boxWidgetClass,
      viewport->widget(), boxArgs, 4);
    viewBox = new xxWidget (boxWidget);
  }

  // Get current settings, but convert the deprecated ones.
  Settings settings;
  settings.nullify();
  settings.applyUserDefaults();
  settings.fixUpDeprecatedSettings();

  static constString toggleChoices[] = {"Yes",
                                        "No",
                                        "(blank)",
                                        NULL};

  static constString unitsChoices[] = {"Feet",
                                       "Meters",
                                       "No preference",
                                       "(blank)",
                                       NULL};

  static constString glChoices[] = {"180",
                                    "150 W",
                                    "120 W",
                                    "90 W",
                                    "60 W",
                                    "30 W",
                                    "0",
                                    "30 E",
                                    "60 E",
                                    "90 E",
                                    "120 E",
                                    "150 E",
                                    "Max stations",
                                    "(blank)",
                                    NULL};

  static constString gsChoices[] = {"d",
				    "l",
				    "s",
				    "(blank)",
				    NULL};

  // Create all of the dialogs.
  for (ConfigurablesMap::iterator it = settings.begin();
       it != settings.end();
       ++it) {
    Configurable &configurable (it->second);
    if (!configurable.caption.isNull()) {
      if (configurable.kind == Configurable::settingKind) {
	switch (configurable.interpretation) {
	case Configurable::booleanInterp:
	  {
	    unsigned firstChoice;
	    if (configurable.isNull)
	      firstChoice = 2;
	    else if (configurable.c == 'n')
	      firstChoice = 1;
	    else
	      firstChoice = 0;
	    dialogs[it->first] = new xxMultiChoice (
						*viewBox,
	                                        configurable.caption.aschar(),
                                                toggleChoices,
                                                firstChoice);
	  }
	  break;
	case Configurable::posIntInterp:
	  dialogs[it->first] = new xxUnsignedChooser (
                                                *viewBox,
	                                        configurable.caption.aschar(),
						configurable.u,
						configurable.isNull,
						configurable.minValue);
	  break;
	case Configurable::posDoubleInterp:
	case Configurable::nonnegativeDoubleInterp:
	case Configurable::opacityDoubleInterp:
	  {
	    char temp[80];
	    if (configurable.isNull)
	      strcpy (temp, "");
	    else
	      sprintf (temp, "%0.2f", configurable.d);
	    dialogs[it->first] = new xxHorizDialog (
                                                *viewBox,
	                                        configurable.caption.aschar(),
						temp);
	  }
	  break;
	case Configurable::glDoubleInterp:
	  {
	    unsigned firstChoice = 13;
	    if (!configurable.isNull) {
	      if (configurable.d == 360.0)
		firstChoice = 12;
	      else
		firstChoice = (unsigned) ((configurable.d + 180.0) / 30.0);
	    }
	    dialogs[it->first] = new xxMultiChoice (
                                                *viewBox,
	                                        configurable.caption.aschar(),
						glChoices,
						firstChoice);
	  }
	  break;
	case Configurable::colorInterp:
	case Configurable::eventMaskInterp:
	case Configurable::timeFormatInterp:
	case Configurable::textInterp:    
	  dialogs[it->first] = new xxHorizDialog (
                           *viewBox,
			   configurable.caption.aschar(),
			   configurable.isNull ? "" : configurable.s.aschar());
	  break;
	case Configurable::unitInterp:
	  {
	    unsigned firstChoice;
	    if (configurable.isNull)
	      firstChoice = 3;
	    else if (configurable.s == "ft")
	      firstChoice = 0;
	    else if (configurable.s == "m")
	      firstChoice = 1;
	    else
	      firstChoice = 2;
	    dialogs[it->first] = new xxMultiChoice (
                                                *viewBox,
						configurable.caption.aschar(),
						unitsChoices,
						firstChoice);
	  }
	  break;
	case Configurable::gsInterp:
	  {
	    unsigned firstChoice;
	    if (configurable.isNull)
	      firstChoice = 3;
	    else if (configurable.c == 'd')
	      firstChoice = 0;
	    else if (configurable.c == 'l')
	      firstChoice = 1;
	    else
	      firstChoice = 2;
	    dialogs[it->first] = new xxMultiChoice (
                                                *viewBox,
						configurable.caption.aschar(),
						gsChoices,
						firstChoice);
	  }
	  break;
	default:
	  assert (false);
	}
      }
    }
  }

  {
    Arg applyArgs[9] =  {
      {XtNvisual, (XtArgVal)xxX::visual},
      {XtNcolormap, (XtArgVal)xxX::colormap},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)viewport->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonWidget = xxX::createXtWidget ("Apply", commandWidgetClass,
      container->widget(), applyArgs, 9);
    XtAddCallback (buttonWidget, XtNcallback, applyCallback,
     (XtPointer)this);
    applyButton = new xxWidget (buttonWidget);
  }{
    Arg saveArgs[10] =  {
      {XtNvisual, (XtArgVal)xxX::visual},
      {XtNcolormap, (XtArgVal)xxX::colormap},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)viewport->widget()},
      {(char*)XtNfromHoriz, (XtArgVal)applyButton->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonWidget = xxX::createXtWidget ("Save", commandWidgetClass,
      container->widget(), saveArgs, 10);
    XtAddCallback (buttonWidget, XtNcallback, saveCallback,
     (XtPointer)this);
    saveButton = new xxWidget (buttonWidget);
  }{
    Arg buttonArgs[10] =  {
      {XtNvisual, (XtArgVal)xxX::visual},
      {XtNcolormap, (XtArgVal)xxX::colormap},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)viewport->widget()},
      {(char*)XtNfromHoriz, (XtArgVal)saveButton->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonWidget = xxX::createXtWidget ("Dismiss", commandWidgetClass,
      container->widget(), buttonArgs, 10);
    XtAddCallback (buttonWidget, XtNcallback, dismissCallback,
     (XtPointer)this);
    dismissButton = new xxWidget (buttonWidget);
  }{
    Arg buttonArgs[10] =  {
      {XtNvisual, (XtArgVal)xxX::visual},
      {XtNcolormap, (XtArgVal)xxX::colormap},
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {(char*)XtNfromVert, (XtArgVal)viewport->widget()},
      {(char*)XtNfromHoriz, (XtArgVal)dismissButton->widget()},
      {(char*)XtNleft, (XtArgVal)XawChainLeft},
      {(char*)XtNright, (XtArgVal)XawChainLeft},
      {(char*)XtNtop, (XtArgVal)XawChainBottom},
      {(char*)XtNbottom, (XtArgVal)XawChainBottom}
    };
    Widget buttonWidget = xxX::createXtWidget ("?", commandWidgetClass,
      container->widget(), buttonArgs, 10);
    XtAddCallback (buttonWidget, XtNcallback, helpCallback,
      (XtPointer)this);
    helpButton = new xxWidget (buttonWidget);
  }

  obeyMouseWheel (popup->widget());
  obeyMouseWheel (scrollbarWidget);
  obeyMouseWheel (viewBox->widget());
}


void xxRoot::dup() {
  ++popupCount;
}


void xxRoot::dup (xxWindow *child) {
  dup();
  children.insert (child);
}


void xxRoot::release () {
  assert (popupCount > 0);
  if (--popupCount == 0)
    exit (0);
}


void xxRoot::release (xxWindow *child) {
  release ();
  children.erase (child);
}


void xxRoot::globalRedraw() {
  for (std::set<xxWindow*>::iterator it = children.begin();
       it != children.end();
       ++it)
    (*it)->globalRedraw();

  for (BetterMap<const Dstr, xxRedrawable*>::iterator it = dialogs.begin();
       it != dialogs.end();
       ++it)
    it->second->globalRedraw();

  Arg buttonArgs[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (applyButton);
  XtSetValues (applyButton->widget(), buttonArgs, 2);
  assert (saveButton);
  XtSetValues (saveButton->widget(), buttonArgs, 2);
  assert (helpButton);
  XtSetValues (helpButton->widget(), buttonArgs, 2);
  assert (dismissButton);
  XtSetValues (dismissButton->widget(), buttonArgs, 2);

  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (label);
  XtSetValues (label->widget(), args, 2);
  assert (viewBox);
  XtSetValues (viewBox->widget(), args, 2);
  assert (viewport);
  XtSetValues (viewport->widget(), args, 2);
  assert (container.get());
  XtSetValues (container->widget(), args, 2);
  assert (popup.get());
  XtSetValues (popup->widget(), args, 2);
  XtSetValues (scrollbarWidget, args, 2);
}


void xxRoot::run() {
  if (!Global::disclaimerDisabled()) {
    (void) new xxDisclaimer (*popup, true);
    handleXEvents (returnWhenIdle);
  }

  if (Global::settings["l"].isNull)
    newChooser ();
  else
    commandLineWindows ();

  handleXEvents (loopForever);
}


xxHelpBox * const xxRoot::newHelpBox (const Dstr &help) {
  return new xxHelpBox (*popup, help);
}


xxGraphMode * const xxRoot::newGraph (Station *station, Timestamp t) {
  return new xxGraphMode (*popup, station, t);
}


xxGraphMode * const xxRoot::newGraph (const StationRef &stationRef) {
  return newGraph (stationRef.load(), (time_t)time(NULL));
}


xxTextMode * const xxRoot::newPlain (Station *station, Timestamp t) {
  return new xxTextMode (*popup, station, Mode::plain, t);
}


xxTextMode * const xxRoot::newRaw (Station *station, Timestamp t) {
  return new xxTextMode (*popup, station, Mode::raw, t);
}


xxTextMode * const xxRoot::newMediumRare (Station *station, Timestamp t) {
  return new xxTextMode (*popup, station, Mode::mediumRare, t);
}


xxClock * const xxRoot::newClock (Station *station,
				  xxClock::ButtonsStyle buttonsStyle) {
  return new xxClock (*popup, station, buttonsStyle);
}


xxClock * const xxRoot::newClock (Station *station) {
  return newClock (station,
                   (Global::settings["cb"].c == 'n' ? xxClock::noButtons :
                                                      xxClock::buttons));
}


void xxRoot::commandLineWindows () {
  Settings &settings (Global::settings);
  Timestamp t ((time_t)time(NULL));
  Mode::Mode mode;
  {
    const Configurable &m (settings["m"]);
    if (m.isNull)
      mode = Mode::clock;
    else
      mode = (Mode::Mode)m.c;
  }{
    const Configurable &l (settings["l"]);
    const Configurable &ml (settings["ml"]);
    DstrVector::const_iterator it (l.v.begin());
    DstrVector::const_iterator stop (l.v.end());
    while (it != stop) {
      StationRef *stationRef (Global::stationIndex().getStationRefByName(*it));
      if (stationRef) {
        const Configurable &b (settings["b"]);
	if (!b.isNull) {
	  t = Timestamp (b.s, stationRef->timezone);
	  if (t.isNull())
	    Global::cant_mktime (b.s, stationRef->timezone, Error::fatal);
	}
        Station *station (stationRef->load());

	if (!ml.isNull) {
	  station->markLevel = ml.p;
	  if (ml.p.Units() != station->predictUnits())
	    station->markLevel.Units (station->predictUnits());
	}

	xxWindow *d (NULL);
	if (mode == Mode::graph)
	  d = newGraph (station, t);
	else if (mode == Mode::plain)
	  d = newPlain (station, t);
	else if (mode == Mode::raw)
	  d = newRaw (station, t);
	else if (mode == Mode::mediumRare)
	  d = newMediumRare (station, t);
	else if (mode == Mode::about) {
          d = newAbout (station);
          delete station;
	} else
	  d = newClock (station);
        assert (d);

	// Apply -geometry to first one only.
	if (it == l.v.begin())
	  d->move (settings["X"].s);

      } else {
	Dstr details ("Could not find: ");
	details += *it;
	Global::barf (Error::STATION_NOT_FOUND, details);
      }
      ++it;
    }
  }
}


void xxRoot::newChooser () {
  if (Global::settings["fe"].c == 'n')
    (void) new xxGlobe (*popup);
  else
    (void) new xxMap (*popup);
}


void xxRoot::newMap () {
  (void) new xxMap (*popup);
}


void xxRoot::newGlobe () {
  (void) new xxGlobe (*popup);
}


static bool lineTooLong (const Dstr &line, const unsigned widthLimit) {
  return (xxX::stringWidth (xxX::monoFontStruct, line) > widthLimit);
}


// This function is not very efficient, but its use is usually avoided.
static void wrapLongLine (Dstr &line,
			  const unsigned widthLimit) {
  Dstr whatFitsOnScreen, wrappedLine;
  while (line.length()) {
    whatFitsOnScreen += line[0];
    if (lineTooLong (whatFitsOnScreen, widthLimit)) {
      whatFitsOnScreen -= whatFitsOnScreen.length()-1;
      if (wrappedLine.length())
	wrappedLine += '\n';
      wrappedLine += whatFitsOnScreen;
      whatFitsOnScreen = (char*)NULL;
    } else
      line /= 1;
  }
  // Don't worry about eating blank lines here--if it were blank it
  // would not have been too long in the first place.
  if (whatFitsOnScreen.length()) {
    if (wrappedLine.length())
      wrappedLine += '\n';
    wrappedLine += whatFitsOnScreen;
  }
  line = wrappedLine;
}


static void wrapLongLines (Dstr &verbiage) {
  assert (WidthOfScreen(xxX::screen) > 0 &&
          (unsigned)WidthOfScreen(xxX::screen) > windowDressingWidth);
  const unsigned widthLimit (WidthOfScreen(xxX::screen) - windowDressingWidth);
  Dstr formattedVerbiage, line;
  verbiage.getline (line);
  while (!line.isNull()) {
    // Skip wrapping for short enough lines (which should be most of them)
    if (lineTooLong (line, widthLimit))
      wrapLongLine (line, widthLimit);
    if (formattedVerbiage.length())
      formattedVerbiage += '\n';
    formattedVerbiage += line;
    verbiage.getline (line);
  }
  verbiage = formattedVerbiage;
}


xxHelpBox * const xxRoot::newAbout (const Station *station) {
  assert (station);
  Dstr verbiage;
  station->aboutMode (verbiage, Format::text, "ISO-8859-1");
  // If necessary, wrap text to make the pop-up fit on the screen.  If this
  // is not done, a station with long descriptive text that is not formatted
  // with linebreaks results in a ludicrously wide window that cannot then be
  // resized.
  wrapLongLines (verbiage);
  return newHelpBox (verbiage);
}


void const xxRoot::newAboutXTide () {
  (void) new xxDisclaimer (*popup, false);
}


void xxRoot::newErrorBox (const Dstr &errmsg, Error::ErrType fatality) {
  static bool snakeBit (false); // Hide double-barfs.
  if (!snakeBit)
    (void) new xxErrorBox (*popup, errmsg, fatality);
  if (fatality == Error::fatal) {
    snakeBit = true;

    // This is a bad thing.  We have to grind crank on the X server to
    // get the error box to appear, but this might also cause
    // previously queued events to do stuff--possibly even leading to
    // another fatal error.  The callback that led to the current sad
    // state of affairs is being held hostage on the stack and did not
    // get to finish whatever it was doing.
    handleXEvents (xxRoot::loopForever);
  }
}


// Similar to, but critically different from, xxWindow::realize.
void xxRoot::realize() {
  if (!_isRealized) {
    Widget popupWidget (popup->widget());
    XtRealizeWidget (popupWidget);
    XSetWMProtocols (xxX::display, XtWindow(popupWidget), &xxX::killAtom, 1);
    _isRealized = true;
    setTitle_NET_WM();
    dup();
  }
}


// Similar to, but critically different from, xxWindow::unrealize.
void xxRoot::unrealize() {
  if (_isRealized) {
    XtUnrealizeWidget (popup->widget());
    _isRealized = false;
    release();

    // Since the window vanishes, the Dismiss button misses the
    // LeaveWindow event.  The next time the window is realized,
    // Dismiss is still stuck in a set and/or highlighted state.
    // This fixes that.
    XtCallActionProc (dismissButton->widget(), "reset", NULL, NULL, 0);
  }
}


xxWindow * const xxRoot::dismiss() {
  unrealize();
  // ISO/IEC 14882:2003 5.3.5 specifies that deleting a null pointer
  // shall have no effect.
  return NULL;
}


void xxRoot::handleXEvents (HandleXEventsReturnProtocol protocol) {
  XEvent foo;
  std::map<Window, XEvent> procrastinatedResizes;
  do {
    bool blockOnce (protocol == loopForever);
    while (XtAppPending(xxX::appContext) || blockOnce) {
      blockOnce = false;
      XtAppNextEvent (xxX::appContext, &foo);
      if (foo.type == ConfigureNotify)
	// Newer window managers like Metacity, Compiz, KWin, Xfwm, and
	// Openbox got the idea to deliver a stream of resize events while a
	// window is being resized instead of just one for the final size.
	// This was not in the contract, but now we have to deal with it to
	// avoid looking stupid.  Put off resizes and discard those that
	// become redundant before we get around to them.
        procrastinatedResizes[foo.xconfigure.window] = foo;
      else
        XtDispatchEvent (&foo);
    }
    // Catch up on resizes when there's nothing better to do.
    for (std::map<Window, XEvent>::iterator it (procrastinatedResizes.begin());
	 it != procrastinatedResizes.end();
	 ++it)
      XtDispatchEvent (&(it->second));
    procrastinatedResizes.clear();
  } while (protocol == loopForever);
}
