// $Id: Configurable.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  Configurable:  A setting or a switch.

    Copyright (C) 2004  David Flater.

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

namespace libxtide {

class PredictionValue;
typedef SafeVector<Dstr> DstrVector;


// Configurable must be an aggregate so that we can initialize it with
// an initializer-list.  That means no inheriting from Nullable even
// though it is, in fact, nullable.  An aggregate may not have a
// constructor.

struct Configurable {

  // Enum values are postfixed with Kind, Rep, and Interp to avoid
  // stepping on reserved words.


  // Settings are those configurables enumerated at
  // http://www.flaterco.com/xtide/settings.html, which have
  // representations as preprocessor defines, X resources, xtide.xml
  // attributes, and command line switches.

  // Switches are those configurables enumerated at
  // http://www.flaterco.com/xtide/tty.html, which are not available
  // in any form except command line switches.

  // Switches recognized by X11 are magically removed from the command
  // line by XtOpenDisplay, so it is not necessary to handle them
  // here.

  enum Kind {settingKind,
             switchKind};


  // Representation tells you which of the data types is used.

  enum Representation {unsignedRep,
                       doubleRep,
                       charRep,
                       dstrRep,
                       predictionValueRep,
                       dstrVectorRep};


  // Interpretation constrains the allowable values.  No value is
  // constrained to be not null.  All constraint checking is done
  // in Settings.cc.

  enum Interpretation {
    booleanInterp,     // constrains charRep to 'y' or 'n'
    posIntInterp,      // constrains unsignedRep to be > 0
    posDoubleInterp,   // constrains doubleRep to be > 0.0
    nonnegativeDoubleInterp, // constrains doubleRep to be >= 0.0
    opacityDoubleInterp, // constrains doubleRep to be >= 0.0 and <= 1.0
    glDoubleInterp,    // constrains doubleRep to have one of the gl values
    modeInterp,        // constrains charRep to be one of the mode characters
    formatInterp,      // constrains charRep to be one of the format characters
    gsInterp,          // constrains charRep to be one of the graphstyle chars
    colorInterp,       // constrains dstrRep to contain a color spec
    unitInterp,        // constrains dstrRep to be "ft", "m", or "x"
    timeFormatInterp,  // constrains dstrRep to be a strftime format string
    eventMaskInterp,   // constrains dstrRep to be a valid eventmask
    numberInterp,      // a scanf %f must succeed on the input
    textInterp         // the input cannot be a null pointer or empty string
  };


  Dstr switchName;     // 2-character ID used on command line and XML
  Dstr resourceName;   // X resource name sans XTide* prefix

  // For use in control panel.  If null, do not offer option in control panel.
  Dstr caption;

  Kind kind;
  Representation representation;
  Interpretation interpretation;

  // See comments at top regarding inheritance from Nullable.
  bool isNull;

  // This glob of attributes is collectively "value."  The space saved
  // by a union would not be worth the inconvenience.

  unsigned        u;
  double          d;
  char            c;
  Dstr            s;
  PredictionValue p;
  DstrVector      v;

  // Only unsigned configurables can have minimum values, so why make
  // it complicated.
  unsigned minValue;
};

// Keyed by switchName.
typedef BetterMap<const Dstr, Configurable> ConfigurablesMap;

}
