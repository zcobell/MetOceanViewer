/* $Id: Settings.cc 6197 2016-01-14 01:10:19Z flaterco $ */

/*  Settings  XTide global settings

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

#include "libxtide.hh"
#include "xmlparser.hh"
#include "config.hh"

namespace libxtide {


Settings::ArgList Settings::commandLineArgs;

static constString legalModes       = "abcCgklmprs";
static constString legalFormats     = "chilptv";
static constString legalGraphStyles = "dls";
static const bool (*_getResource) (const Dstr &resourceName, Dstr &val_out)
  = NULL;
static bool commandLineCached = false;


static void setxmlfilename () {
//#ifdef UseLocalFiles
//  xmlfilename = ".xtide.xml";
//#else
//  xmlfilename = getenv ("HOME");
//  if (xmlfilename.isNull())
//    return;
//  xmlfilename += "/.xtide.xml";
//#endif
}


static void freexml (xmlattribute *v);

static void freexml (xmltag *v) {
  if (!v)
    return;
  freexml (v->next);
  freexml (v->contents);
  freexml (v->attributes);
  delete v->name;
  delete v;
}

static void freexml (xmlattribute *v) {
  if (!v)
    return;
  freexml (v->next);
  delete v->value;
  delete v->name;
  delete v;
}


static void contentOrNull (Dstr &details, const Dstr &val) {
  if (val.isNull())
    details += "NULL";
  else if (val.length() == 0)
    details += "an empty string";
  else {
    details += "'";
    details += val;
    details += "'";
  }
}


// Constraint checking.  True return means it's bogus.
// Culprit:  who to blame in error message if value is bogus.
// If null, no error is printed; instead, function returns true.

static const bool checkBoolean (const Dstr &culprit, char val) {
  if (val != 'y' && val != 'n') {
    if (!culprit.isNull()) {
      Dstr details ("The offending input in ");
      details += culprit;
      details += " was '";
      details += val;
      details += "' (expecting 'y' or 'n').";
      Global::barf (Error::BAD_BOOL, details);
    }
    return true;
  }
  return false;
}


static const bool checkPositiveInteger (const Dstr &culprit, int val) {
  if (val <= 0) {
    if (!culprit.isNull()) {
      Dstr details ("The offending input in ");
      details += culprit;
      details += " was '";
      details += val;
      details += "' (expecting a positive integer).";
      Global::barf (Error::NUMBER_RANGE_ERROR, details);
    }
    return true;
  }
  return false;
}


static const bool checkPositiveDouble (const Dstr &culprit, double val) {
  if (val <= 0.0) {
    if (!culprit.isNull()) {
      Dstr details ("The offending input in ");
      details += culprit;
      details += " was '";
      details += val;
      details += "' (expecting a positive double).";
      Global::barf (Error::NUMBER_RANGE_ERROR, details);
    }
    return true;
  }
  return false;
}


static const bool checkNonnegativeDouble (const Dstr &culprit, double val) {
  if (val < 0.0) {
    if (!culprit.isNull()) {
      Dstr details ("The offending input in ");
      details += culprit;
      details += " was '";
      details += val;
      details += "' (expecting a nonnegative double).";
      Global::barf (Error::NUMBER_RANGE_ERROR, details);
    }
    return true;
  }
  return false;
}


static const bool checkOpacityDouble (const Dstr &culprit, double val) {
  if (val < 0.0 || val > 1.0) {
    if (!culprit.isNull()) {
      Dstr details ("The offending input in ");
      details += culprit;
      details += " was '";
      details += val;
      details += "' (expecting a double between 0 and 1).";
      Global::barf (Error::NUMBER_RANGE_ERROR, details);
    }
    return true;
  }
  return false;
}


static const bool checkGLdouble (const Dstr &culprit, double val) {
  if (val != -180.0 &&
      val != -150.0 &&
      val != -120.0 &&
      val != -90.0 &&
      val != -60.0 &&
      val != -30.0 &&
      val !=  0.0 &&
      val !=  30.0 &&
      val !=  60.0 &&
      val !=  90.0 &&
      val !=  120.0 &&
      val !=  150.0 &&
      val !=  360.0) {
    if (!culprit.isNull()) {
      Dstr details ("The offending input in ");
      details += culprit;
      details += " was '";
      details += val;
      details += "' (expecting one of the following:\n";
      details += "-180 -150 -120 -90 -60 -30 0 30 60 90 120 150 360).";
      Global::barf (Error::NUMBER_RANGE_ERROR, details);
    }
    return true;
  }
  return false;
}


static const bool checkMode (const Dstr &culprit, char val) {
  if (strchr (legalModes, val))
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was '";
    details += val;
    details += "' (expecting one of ";
    details += legalModes;
    details += ").";
    Global::barf (Error::BAD_MODE, details);
  }
  return true;
}


static const bool checkMode (const Dstr &culprit, constCharPointer val) {
  if (val)
    if (strlen (val) == 1)
      return checkMode (culprit, *val);
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += " (expecting one of ";
    details += legalModes;
    details += ").";
    Global::barf (Error::BAD_MODE, details);
  }
  return true;
}


static const bool checkFormat (const Dstr &culprit, char val) {
  if (strchr (legalFormats, val))
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was '";
    details += val;
    details += "' (expecting one of ";
    details += legalFormats;
    details += ").";
    Global::barf (Error::BAD_FORMAT, details);
  }
  return true;
}


static const bool checkFormat (const Dstr &culprit, constCharPointer val) {
  if (val)
    if (strlen (val) == 1)
      return checkFormat (culprit, *val);
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += " (expecting one of ";
    details += legalFormats;
    details += ").";
    Global::barf (Error::BAD_FORMAT, details);
  }
  return true;
}


static const bool checkGraphStyle (const Dstr &culprit, char val) {
  if (strchr (legalGraphStyles, val))
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was '";
    details += val;
    details += "' (expecting one of ";
    details += legalGraphStyles;
    details += ").";
    Global::barf (Error::BAD_GRAPHSTYLE, details);
  }
  return true;
}


static const bool checkGraphStyle (const Dstr &culprit, constCharPointer val) {
  if (val)
    if (strlen (val) == 1)
      return checkGraphStyle (culprit, *val);
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += " (expecting one of ";
    details += legalGraphStyles;
    details += ").";
    Global::barf (Error::BAD_GRAPHSTYLE, details);
  }
  return true;
}


static const bool checkColor (const Dstr &culprit, const Dstr &val) {
  // This is not very rigorous checking, but Parsecolor wants to print
  // warnings.  More rigorous version in xxRoot.
  char c = val[0];
  if (isalpha(c) || c == '#')
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += '.';
    Global::barf (Error::BADCOLORSPEC, details);
  }
  return true;
}


static const bool checkUnit (const Dstr &culprit, const Dstr &val) {
  if (val == "ft" || val == "m" || val == "x")
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += '.';
    Global::barf (Error::UNRECOGNIZED_UNITS, details);
  }
  return true;
}


static const bool checkNumber (const Dstr &culprit, constCharPointer val) {
  float f;
  if (val)
    if (sscanf (val, "%f", &f) == 1)
      return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += '.';
    Global::barf (Error::NOT_A_NUMBER, details);
  }
  return true;
}


static const bool checkText (const Dstr &culprit, const Dstr &val) {
  if (val.length())
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += '.';
    Global::barf (Error::BAD_TEXT, details);
  }
  return true;
}


static const bool checkStrftimeFormat (const Dstr &culprit, const Dstr &val) {
  return checkText (culprit, val);
}


static const bool checkEventMask (const Dstr &culprit, const Dstr &val) {
  if (Global::isValidEventMask (val))
    return false;
  if (!culprit.isNull()) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += '.';
    Global::barf (Error::BAD_EVENTMASK, details);
  }
  return true;
}


static const bool checkConfigurable (const Dstr &culprit,
				     const Configurable &val) {
  if (val.isNull)
    return false;
  switch (val.interpretation) {
  case Configurable::booleanInterp:
    assert (val.representation == Configurable::charRep);
    return checkBoolean (culprit, val.c);
  case Configurable::posIntInterp:
    assert (val.representation == Configurable::unsignedRep);
    return checkPositiveInteger (culprit, val.u);
  case Configurable::posDoubleInterp:
    assert (val.representation == Configurable::doubleRep);
    return checkPositiveDouble (culprit, val.d);
  case Configurable::nonnegativeDoubleInterp:
    assert (val.representation == Configurable::doubleRep);
    return checkNonnegativeDouble (culprit, val.d);
  case Configurable::opacityDoubleInterp:
    assert (val.representation == Configurable::doubleRep);
    return checkOpacityDouble (culprit, val.d);
  case Configurable::glDoubleInterp:
    assert (val.representation == Configurable::doubleRep);
    return checkGLdouble (culprit, val.d);
  case Configurable::modeInterp:
    assert (val.representation == Configurable::charRep);
    return checkMode (culprit, val.c);
  case Configurable::formatInterp:
    assert (val.representation == Configurable::charRep);
    return checkFormat (culprit, val.c);
  case Configurable::gsInterp:
    assert (val.representation == Configurable::charRep);
    return checkGraphStyle (culprit, val.c);
  case Configurable::colorInterp:
    assert (val.representation == Configurable::dstrRep);
    return checkColor (culprit, val.s);
  case Configurable::unitInterp:
    assert (val.representation == Configurable::dstrRep);
    return checkUnit (culprit, val.s);
  case Configurable::numberInterp:
    // If you made it here, it must be OK.
    return false;
  case Configurable::timeFormatInterp:
    assert (val.representation == Configurable::dstrRep);
    return checkStrftimeFormat (culprit, val.s);
  case Configurable::eventMaskInterp:
    assert (val.representation == Configurable::dstrRep);
    return checkEventMask (culprit, val.s);
  case Configurable::textInterp:
    if (val.representation == Configurable::dstrRep)
      return checkText (culprit, val.s);
    else {
      assert (val.representation == Configurable::dstrVectorRep);
      DstrVector::const_iterator it = val.v.begin();
      while (it != val.v.end()) {
        if (checkText (culprit, *it))
          return true;
        ++it;
      }
      return false;
    }
  default:
    assert (false);
  }
  return false; // Silence compiler warning
}


// Not using Global::getDouble because error reporting is different here.
static const double getDouble (const Dstr &culprit, const Dstr &val) {
  double temp;
  if (sscanf (val.aschar(), "%lf", &temp) != 1) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += " (expecting a double).";
    Global::barf (Error::NOT_A_NUMBER, details);
  }
  return temp;
}


static const unsigned getPositiveInteger (const Dstr &culprit,
					  const Dstr &val) {
  int temp;
  if (sscanf (val.aschar(), "%d", &temp) != 1) {
    Dstr details ("The offending input in ");
    details += culprit;
    details += " was ";
    contentOrNull (details, val);
    details += " (expecting a positive integer).";
    Global::barf (Error::NOT_A_NUMBER, details);
  }
  require (!checkPositiveInteger (culprit, temp));
  return temp;
}


// Attempt to update an individual setting.
static void install (Configurable &configurable,
                     const Dstr &culprit,
                     const Dstr &val) {
  configurable.isNull = false;
  switch (configurable.representation) {
  case Configurable::unsignedRep:
    configurable.u = std::max (configurable.minValue,
		               getPositiveInteger (culprit, val));
    break;
  case Configurable::doubleRep:
    configurable.d = getDouble (culprit, val);
    break;
  case Configurable::charRep:
    configurable.c = val[0];
    break;
  case Configurable::dstrRep:
    configurable.s = val;
    break;
  case Configurable::predictionValueRep:
    {
      unsigned i;
      for (i=0; i<val.length(); ++i) {
	char c = val[i];
	if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'
	|| c == 'e' || c == 'E'))
	  break;
      }
      Dstr uts (val.ascharfrom(i));
      double v (getDouble (culprit, val));
      configurable.p = PredictionValue (Units::parse(uts), v);
    }
    break;
  case Configurable::dstrVectorRep:
    configurable.v.push_back (val);
    break;
  default:
    assert (false);
  }
  require (!checkConfigurable (culprit, configurable));
}


// -----------------------------------------------------------------------


// Command line parser.  See Settings.hh for documentation.


void Settings::disambiguate (int argc,
                             constStringArray argv,
                             int argi,
                             ArgList &al_out) {
  al_out.clear();
  constCharPointer argii = argv[argi];
  if (*argii != '-' && *argii != '+')
    return;
  bool isPlus = (*argii == '+');
  ++argii;

  Dstr cookedSwitchName;
  for (ConfigurablesMap::iterator it = begin(); it != end(); ++it) {
    Configurable &cfbl = it->second;
    if (!isPlus || cfbl.interpretation == Configurable::booleanInterp) {
      char *name = cfbl.switchName.aschar();
      if (!strncmp (argii, name, strlen(name))) {

        // Try 1:  concatenated argument
        if (ambiguous (argc, argv, argi, argii + strlen(name),
		       cfbl.interpretation, al_out, name, cookedSwitchName))
          return;

	// Try 2:  separate argument
	if ((*(argii + strlen(name)) == '\0') && (argi + 1 < argc))
          if (ambiguous (argc, argv, argi+1, argv[argi+1],
			 cfbl.interpretation, al_out, name, cookedSwitchName))
            return;
      }
    }
  }

  if (!al_out.empty()) {
    // checkArg is supposed to allocate one for us with a null switchName.
    CommandLineArg &ac = al_out.front();
    assert (ac.switchName.isNull());
    ac.switchName = cookedSwitchName;
    if (isPlus) {
      if (ac.arg == "y")
	ac.arg = 'n';
      else if (ac.arg == "n")
	ac.arg = 'y';
      else
	assert (false);
    }
  }
}


void Settings::checkArg (int argc,
                         constStringArray argv,
                         int argi,
                         constString argii,
                         Configurable::Interpretation argInterpretation,
                         ArgList &al_out) {
  al_out.clear();
  Dstr myCookedArg;
  switch (argInterpretation) {

  // Don't use checkBoolean here; cut some slack.
  case Configurable::booleanInterp:
    assert (argii);
    switch (*argii) {
    case '\0':
    case 'y':
    case 'Y':
      myCookedArg = 'y'; // Standardize value
      break;
    case 'n':
    case 'N':
      myCookedArg = 'n'; // Standardize value
      break;
    default:
      return;
    }
    break;

  case Configurable::posIntInterp:
  case Configurable::posDoubleInterp:
  case Configurable::opacityDoubleInterp:
  case Configurable::nonnegativeDoubleInterp:
  case Configurable::glDoubleInterp:
  case Configurable::numberInterp:
    // Better checking done later.
    if (checkNumber ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::modeInterp:
    if (checkMode ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::formatInterp:
    if (checkFormat ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::gsInterp:
    if (checkGraphStyle ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::colorInterp:
    if (checkColor ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::unitInterp:
    if (checkUnit ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::textInterp:
    if (checkText ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::timeFormatInterp:
    if (checkStrftimeFormat ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  case Configurable::eventMaskInterp:
    if (checkEventMask ((char*)NULL, argii))
      return;
    myCookedArg = argii;
    break;

  default:
    assert (false);
  }

  if (++argi < argc) {
    disambiguate (argc, argv, argi, al_out);
    if (al_out.empty())
      return;
  }
  CommandLineArg newarg;
  // newarg.switchName intentionally left blank
  newarg.arg = myCookedArg;
  al_out.push_front (newarg);
}


const bool Settings::ambiguous (int argc,
				constStringArray argv,
				int argi,
				constString argii,
				Configurable::Interpretation argInterpretation,
				ArgList &arglist,
				constString switchName,
				Dstr &cookedSwitchName_out) {
  if (arglist.empty()) {
    checkArg (argc, argv, argi, argii, argInterpretation, arglist);
    if (!arglist.empty())
      cookedSwitchName_out = switchName;
  } else {
    ArgList ambiguate;
    checkArg (argc, argv, argi, argii, argInterpretation, ambiguate);
    if (!ambiguate.empty()) {
      arglist.clear();
      return true;
    }
  }
  return false;
}


// -----------------------------------------------------------------------


Settings::Settings () {

  // Can initialize a map with C++11 extended initializer lists, but this
  // code ain't broke.

  // Switches recognized by X11 are magically removed from the command
  // line by XtOpenDisplay, so it is not necessary to list them here.

  Configurable cd[] = {
    {"bg", "background", "Background color for text windows and location chooser.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,bgdefcolor,PredictionValue(),DstrVector(), 0},
    {"fg", "foreground", "Color of text and other notations.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,fgdefcolor,PredictionValue(),DstrVector(), 0},
    {"bc", "buttoncolor", "Background color of buttons.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,buttondefcolor,PredictionValue(),DstrVector(), 0},
    {"cc", "currentdotcolor", "Color of dots indicating current stations in the location chooser.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,currentdotdefcolor,PredictionValue(),DstrVector(), 0},
    {"dc", "daycolor", "Daytime background color in tide graphs.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,daydefcolor,PredictionValue(),DstrVector(), 0},
    {"Dc", "datumcolor", "Color of datum line in tide graphs.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,datumdefcolor,PredictionValue(),DstrVector(), 0},
    {"ec", "ebbcolor", "Foreground color in tide graphs during outgoing tide.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,ebbdefcolor,PredictionValue(),DstrVector(), 0},
    {"fc", "floodcolor", "Foreground color in tide graphs during incoming tide.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,flooddefcolor,PredictionValue(),DstrVector(), 0},
    {"mc", "markcolor", "Color of mark line in graphs.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,markdefcolor,PredictionValue(),DstrVector(), 0},
    {"Mc", "mslcolor", "Color of middle-level line in tide graphs.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,msldefcolor,PredictionValue(),DstrVector(), 0},
    {"nc", "nightcolor", "Nighttime background color in tide graphs.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,nightdefcolor,PredictionValue(),DstrVector(), 0},
    {"tc", "tidedotcolor", "Color of dots indicating tide stations in the location chooser.", Configurable::settingKind, Configurable::dstrRep, Configurable::colorInterp, false, 0,0,0,tidedotdefcolor,PredictionValue(),DstrVector(), 0},
    {"to", "tideopacity", "Opacity of the fill in graph style s (0-1).", Configurable::settingKind, Configurable::doubleRep, Configurable::opacityDoubleInterp, false, 0,deftideopacity,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"gt", "graphtenths", "Label tenths of units in tide graphs?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,graphtenths,Dstr(),PredictionValue(),DstrVector(), 0},
    {"el", "extralines", "Draw datum and middle-level lines in tide graphs?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,extralines,Dstr(),PredictionValue(),DstrVector(), 0},
    {"fe", "flatearth", "Prefer flat map to round globe location chooser?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,flatearth,Dstr(),PredictionValue(),DstrVector(), 0},
    {"cb", "cbuttons", "Create tide clocks with buttons?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,cbuttons,Dstr(),PredictionValue(),DstrVector(), 0},
    {"in", "infer", "Use inferred constituents (expert only)?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,infer,Dstr(),PredictionValue(),DstrVector(), 0},
    {"ou", "omitunits", "Print numbers with no ft/m/kt?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,omitunits,Dstr(),PredictionValue(),DstrVector(), 0},
    {"pb", "pagebreak", "Pagebreak and header before every month of a calendar?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,pagebreak,Dstr(),PredictionValue(),DstrVector(), 0},
    {"lb", "linebreak", "Linebreak before prediction value in calendars?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,linebreak,Dstr(),PredictionValue(),DstrVector(), 0},
    {"em", "eventmask", "Event mask:", Configurable::settingKind, Configurable::dstrRep, Configurable::eventMaskInterp, false, 0,0,0,eventmask,PredictionValue(),DstrVector(), 0},
    {"tl", "toplines", "Draw depth lines on top of tide graph?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,toplines,Dstr(),PredictionValue(),DstrVector(), 0},
    {"z", "zulu", "Coerce all time zones to UTC?", Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,forceZuluTime,Dstr(),PredictionValue(),DstrVector(), 0},
    {"cw", "cwidth", "Initial width for tide clocks (pixels):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, std::max(Global::minGraphWidth,defcwidth),0,0,Dstr(),PredictionValue(),DstrVector(), Global::minGraphWidth},
    {"ch", "cheight", "Initial height for tide clocks (pixels):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, std::max(Global::minGraphHeight,defcheight),0,0,Dstr(),PredictionValue(),DstrVector(), Global::minGraphHeight},
    {"gw", "gwidth", "Initial width for tide graphs (pixels):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, std::max(Global::minGraphWidth,defgwidth),0,0,Dstr(),PredictionValue(),DstrVector(), Global::minGraphWidth},
    {"gh", "gheight", "Initial height for tide graphs (pixels):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, std::max(Global::minGraphHeight,defgheight),0,0,Dstr(),PredictionValue(),DstrVector(), Global::minGraphHeight},
    {"tw", "ttywidth", "Width of text format (characters):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, std::max(Global::minTTYwidth,defttywidth),0,0,Dstr(),PredictionValue(),DstrVector(), Global::minTTYwidth},
    {"th", "ttyheight", "Height of ASCII graphs and clocks (characters):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, std::max(Global::minTTYheight,defttyheight),0,0,Dstr(),PredictionValue(),DstrVector(), Global::minTTYheight},
    {"pi", "predictinterval", "Default predict interval (days):", Configurable::settingKind, Configurable::unsignedRep, Configurable::posIntInterp, false, 4,0,0,Dstr(),PredictionValue(),DstrVector(), 1},
    {"ga", "gaspect", "Initial aspect for tide graphs.", Configurable::settingKind, Configurable::doubleRep, Configurable::posDoubleInterp, false, 0,defgaspect,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"lw", "lwidth", "Width of line in graph styles l and s (pixels, pos. real number).", Configurable::settingKind, Configurable::doubleRep, Configurable::posDoubleInterp, false, 0,deflwidth,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"mf", "monofont", "Monospace font (requires restart):", Configurable::settingKind, Configurable::dstrRep, Configurable::textInterp, false, 0,0,0,defmonofont,PredictionValue(),DstrVector(), 0},
    {"gf", "graphfont", "Graph/clock font (requires restart):", Configurable::settingKind, Configurable::dstrRep, Configurable::textInterp, false, 0,0,0,defgraphfont,PredictionValue(),DstrVector(), 0},
    {"ph", "pageheight", "Nominal length of paper in LaTeX output (mm).", Configurable::settingKind, Configurable::doubleRep, Configurable::posDoubleInterp, false, 0,defpageheight,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"pm", "pagemargin", "Nominal width of margins in LaTeX output (mm).", Configurable::settingKind, Configurable::doubleRep, Configurable::nonnegativeDoubleInterp, false, 0,defpagemargin,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"pw", "pagewidth", "Nominal width of paper in LaTeX output (mm).", Configurable::settingKind, Configurable::doubleRep, Configurable::posDoubleInterp, false, 0,defpagewidth,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"gl", "globelongitude", "Initial center longitude for globe:", Configurable::settingKind, Configurable::doubleRep, Configurable::glDoubleInterp, false, 0,defgl,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"cf", "caldayfmt", "Strftime style format string for printing days in calendars.", Configurable::settingKind, Configurable::dstrRep, Configurable::timeFormatInterp, false, 0,0,0,caldayfmt,PredictionValue(),DstrVector(), 0},
    {"df", "datefmt", "Strftime style format string for printing dates.", Configurable::settingKind, Configurable::dstrRep, Configurable::timeFormatInterp, false, 0,0,0,datefmt,PredictionValue(),DstrVector(), 0},
    {"hf", "hourfmt", "Strftime style format string for printing hour labels on time axis.", Configurable::settingKind, Configurable::dstrRep, Configurable::timeFormatInterp, false, 0,0,0,hourfmt,PredictionValue(),DstrVector(), 0},
    {"tf", "timefmt", "Strftime style format string for printing times.", Configurable::settingKind, Configurable::dstrRep, Configurable::timeFormatInterp, false, 0,0,0,timefmt,PredictionValue(),DstrVector(), 0},
    {"gs", "graphstyle", "Style of graphs and clocks:", Configurable::settingKind, Configurable::charRep, Configurable::gsInterp, false, 0,0,defgraphstyle,Dstr(),PredictionValue(),DstrVector(), 0},
    {"u", "units", "Preferred units of length:", Configurable::settingKind, Configurable::dstrRep, Configurable::unitInterp, false, 0,0,0,prefunits,PredictionValue(),DstrVector(), 0},

    {"v", Dstr(), Dstr(), Configurable::switchKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,'n',Dstr(),PredictionValue(),DstrVector(), 0},
    {"suck", Dstr(), Dstr(), Configurable::switchKind, Configurable::charRep, Configurable::booleanInterp, false, 0,0,'n',Dstr(),PredictionValue(),DstrVector(), 0},
    {"b", Dstr(), Dstr(), Configurable::switchKind, Configurable::dstrRep, Configurable::numberInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"e", Dstr(), Dstr(), Configurable::switchKind, Configurable::dstrRep, Configurable::numberInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"s", Dstr(), Dstr(), Configurable::switchKind, Configurable::dstrRep, Configurable::numberInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"f", Dstr(), Dstr(), Configurable::switchKind, Configurable::charRep, Configurable::formatInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"m", Dstr(), Dstr(), Configurable::switchKind, Configurable::charRep, Configurable::modeInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"l", Dstr(), Dstr(), Configurable::switchKind, Configurable::dstrVectorRep, Configurable::textInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"ml", Dstr(), Dstr(), Configurable::switchKind, Configurable::predictionValueRep, Configurable::numberInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"o", Dstr(), Dstr(), Configurable::switchKind, Configurable::dstrRep, Configurable::textInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},

    // Deprecated settings
    {"ns", "nosunmoon", Dstr(), Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    {"nf", "nofill", Dstr(), Configurable::settingKind, Configurable::charRep, Configurable::booleanInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},

    // "X" is where the X geometry string ends up.
    {"X", Dstr(), Dstr(), Configurable::switchKind, Configurable::dstrRep, Configurable::textInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},
    // "XX" is where the X font string ends up if HAVE_XAW3DXFT.
    {"XX", "font", Dstr(), Configurable::settingKind, Configurable::dstrRep, Configurable::textInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0},

    {Dstr(), Dstr(), Dstr(), Configurable::switchKind, Configurable::charRep, Configurable::textInterp, true, 0,0,0,Dstr(),PredictionValue(),DstrVector(), 0}
};

  for (unsigned i=0; !cd[i].switchName.isNull(); ++i) {
    Dstr culprit ("the config.hh definition for ");
    culprit += cd[i].switchName;
    culprit += '/';
    culprit += cd[i].resourceName;
    require (!checkConfigurable (culprit, cd[i]));
    operator[](cd[i].switchName) = cd[i];
  }
}


void Settings::nullify() {
  for (ConfigurablesMap::iterator it = begin(); it != end(); ++it)
    it->second.isNull = true;
}


void Settings::apply (const Settings &settings) {
  for (ConfigurablesMap::const_iterator it = settings.begin();
       it != settings.end();
       ++it) {
    const Configurable &cfbl = it->second;
    if (!cfbl.isNull)
      operator[](it->first) = cfbl;
  }
}


void Settings::applyUserDefaults () {
//  setxmlfilename();
//  if (xmlfilename.isNull())
//    return;

//  xmlparsetree = NULL;
//  if ((yyin = fopen (xmlfilename.aschar(), "rb"))) {
//    yyparse();
//    fclose (yyin);
//    xmltag *tag = xmlparsetree;
//    while (tag) {
//      if ((*(tag->name)) == "xtideoptions") {
//        xmlattribute *a = tag->attributes;
//        while (a) {
//          // Ignore any settings that we don't already have.
//          ConfigurablesMap::iterator it = find (*(a->name));
//          if (it != end()) {
//            Configurable &cfbl = it->second;
//            if (cfbl.kind == Configurable::settingKind) {
//              Dstr culprit ("the ~/.xtide.xml attribute for ");
//              culprit += cfbl.switchName;
//              install (cfbl, culprit, *(a->value));
//            }
//          }
//          a = a->next;
//        }
//      }
//      tag = tag->next;
//    }
//  }
//  freexml (xmlparsetree);
//  xmlparsetree = NULL;
}


void Settings::applyXResources () {
  assert (_getResource);
  for (ConfigurablesMap::iterator it = begin(); it != end(); ++it) {
    Configurable &cfbl = it->second;
    if (cfbl.kind == Configurable::settingKind) {
      Dstr val;
      if ((*_getResource) (cfbl.resourceName, val)) {
        Dstr culprit ("the X resource for ");
        culprit += cfbl.resourceName;
        install (cfbl, culprit, val);
      }
    }
  }
}


void Settings::applyXResources (
         const bool (&getResource) (const Dstr &resourceName, Dstr &val_out)) {
  assert (!_getResource);
  _getResource = &getResource;
  applyXResources();
}


void Settings::save() {
//  setxmlfilename();
//  if (xmlfilename.isNull())
//    Global::barf (Error::NOHOMEDIR);

//  FILE *fp = fopen (xmlfilename.aschar(), "wb");
//  if (!fp) {
//    Global::cantOpenFile (xmlfilename, Error::nonfatal);
//    return;
//  }
//  fprintf (fp, "<?xml version=\"1.0\"?>\n<xtideoptions ");

//  for (ConfigurablesMap::iterator it = begin(); it != end(); ++it) {
//    Configurable &cfbl = it->second;
//    if (cfbl.kind == Configurable::settingKind && !cfbl.isNull) {
//      fprintf (fp, "%s=\"", cfbl.switchName.aschar());
//      switch (cfbl.representation) {
//      case Configurable::unsignedRep:
//        fprintf (fp, "%u", cfbl.u);
//        break;
//      case Configurable::doubleRep:
//        fprintf (fp, "%0.2f", cfbl.d);
//        break;
//      case Configurable::charRep:
//        fprintf (fp, "%c", cfbl.c);
//        break;
//      case Configurable::dstrRep:
//        fprintf (fp, "%s", cfbl.s.aschar());
//        break;
//      default:
//        assert (false);
//      }
//      fprintf (fp, "\"\n");
//    }
//  }
//  fprintf (fp, "/>\n");
//  fclose (fp);
}


void Settings::applyCommandLine () {
  assert (commandLineCached);
  for (ArgList::iterator arglooper = commandLineArgs.begin();
       arglooper != commandLineArgs.end();
       ++arglooper) {
    Configurable &cfbl (operator[](arglooper->switchName));
    Dstr culprit ("the command line argument for ");
    culprit += cfbl.switchName;
    install (cfbl, culprit, arglooper->arg);
  }

  Configurable &loc = operator[]("l");
  if (loc.isNull || loc.v.empty()) {
    loc.v.clear();
    char *defloc = getenv("XTIDE_DEFAULT_LOCATION");
    if (defloc) {
      loc.isNull = false;
      loc.v.push_back (defloc);
      require (!checkConfigurable ("XTIDE_DEFAULT_LOCATION", loc));
    }
  }
}


void Settings::applyCommandLine (int argc, constStringArray argv) {
  assert (!commandLineCached);
  assert (commandLineArgs.empty());

  if (argc > 1) {
    disambiguate (argc, argv, 1, commandLineArgs);
    if (commandLineArgs.empty())
      Global::barf (Error::BAD_OR_AMBIGUOUS_COMMAND_LINE);
  }

  for (ArgList::iterator it = commandLineArgs.begin();
                         it != commandLineArgs.end();
                         ++it)
    if (it->switchName == "v" && it->arg == "y") {
      Global::log (XTideVersionString, LOG_INFO);
      exit (0);
    }

  commandLineCached = true;
  applyCommandLine();
}


void Settings::fixUpDeprecatedSettings() {
  Configurable &nsConfig (operator[]("ns"));
  if (!nsConfig.isNull) {
    static bool warnOnce = false;
    if (!warnOnce) {
      warnOnce = true;
      Global::log ("\
XTide Warning:  The nosunmoon setting (command line -ns, or \"Suppress sun\n\
and moon events in output?\" in control panel) is obsolete.  For equivalent\n\
results, set eventmask (command line -em, or \"Event mask:\" in control\n\
panel) to the value pSsMm.  Documentation on settings is at\n\
http://www.flaterco.com/xtide/settings.html.",
        LOG_WARNING);
    }
    Configurable &emConfig (operator[]("em"));
    if (nsConfig.c == 'y')
      emConfig.s = "pSsMm";
    else
      emConfig.s = "x";
    nsConfig.isNull = true;
    emConfig.isNull = false;
    // If they provide both em and ns, em will be clobbered.
  }

  Configurable &nfConfig (operator[]("nf"));
  if (!nfConfig.isNull) {
    static bool warnOnce = false;
    if (!warnOnce) {
      warnOnce = true;
      Global::log ("\
XTide Warning:  The nofill setting (command line -nf, or \"Draw tide graphs\n\
as line graphs?\" in control panel) is obsolete.  For equivalent results, set\n\
graphstyle (command line -gs, or \"Style of graphs and clocks:\" in control\n\
panel) to the value l.  Documentation on settings is at\n\
http://www.flaterco.com/xtide/settings.html.",
        LOG_WARNING);
    }
    Configurable &gsConfig (operator[]("gs"));
    if (nfConfig.c == 'y')
      gsConfig.c = 'l';
    else
      gsConfig.c = 'd';
    nfConfig.isNull = true;
    gsConfig.isNull = false;
    // If they provide both nf and gs, gs will be clobbered.
  }
}

}
