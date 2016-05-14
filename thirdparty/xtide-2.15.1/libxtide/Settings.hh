// $Id: Settings.hh 5748 2014-10-11 19:38:53Z flaterco $

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


namespace libxtide {

// "STL containers are not intended to be used as base classes (their
// destructors are deliberately non-virtual).  Deriving from a
// container is a common mistake made by novices."
// -- Standard Template Library,
// http://en.wikipedia.org/w/index.php?title=Standard_Template_Library&oldid=98705028
// (last visited January 13, 2007).

class Settings: public ConfigurablesMap {
public:

  // Default constructor initializes map to config.hh defaults.  This
  // is desirable even if you intend to call nullify() immediately.
  // An empty map tells you nothing about what settings are even
  // available.  A nulled-out map gives you all the metadata (just no
  // data).
  Settings();

  // Null out all settings.
  void nullify();

  // Supersede by ~/.xtide.xml contents.  If HOME is unset, do nothing.
  void applyUserDefaults ();

  // Supersede by command line.
  // Parsed command line is cached for future reuse.
  void applyCommandLine (int argc,               // Call exactly once.
                         constStringArray argv);
  void applyCommandLine ();                      // Call other one first.

  // Supersede by X resources.  We avoid linkage difficulties for tide
  // and xttpd by taking a function as argument.
  void applyXResources (                         // Call exactly once.
    const bool (&getResource) (const Dstr &resourceName, Dstr &val_out));
  void applyXResources ();                       // Call other one first.

  // Supersede by new settings from control panel.
  void apply (const Settings &settings);

  // Call this last.
  void fixUpDeprecatedSettings();

  // Write to ~/.xtide.xml.  Barf if HOME is unset.
  void save();

protected:

  // Everything down here is for the command line parser.

  struct CommandLineArg {
    Dstr switchName;
    Dstr arg;
  };

  // slist is not standard.  It goes something like
  //   #include <ext/slist>
  //   typedef __gnu_cxx::slist<CommandLineArg> ArgList;
  typedef std::list<CommandLineArg> ArgList;

  // The command line is disambiguated once and retained here.
  static ArgList commandLineArgs;

  // The goal is to disambiguate the argument string.
  void disambiguate (int argc,               // The usual.
                     constStringArray argv,  // The usual.
                     int argi,     // Index of argument now being looked at.
                     ArgList &al_out); // Disambiguated argument list if valid;
                                       // empty if not valid.

  // The goal is still to disambiguate the argument string.
  void checkArg (int argc,              // The usual.
                 constStringArray argv, // The usual.
                 int argi,    // Index of argument now being looked at.
                 // Current character position in argv[argi].
                 constString argii,
                 // "Type" of argument we expect to find there.
		 Configurable::Interpretation argInterpretation,
                 // Disambiguated argument list if valid; empty if not valid.
                 ArgList &al_out);

  // This is a wrapper for checkArg to eliminate duplicated code in
  // disambiguate.  If arglist is not already full of one possible
  // interpretation of the ArgList, it calls checkArg on arglist and
  // sets cookedSwitchName to switchName if that succeeds.  If arglist
  // already contains one possible interpretation of the ArgList, it
  // calls checkArg on a temporary and, if that succeeds, clears
  // arglist and returns true.
  const bool ambiguous (int argc,
			constStringArray argv,
			int argi,
			constString argii,
			Configurable::Interpretation argInterpretation,
			ArgList &arglist,
			constString switchName,
			Dstr &cookedSwitchName_out);
};

}
