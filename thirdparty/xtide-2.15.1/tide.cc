// $Id: tide.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  tide  Command-line client for dusty old TTYs and line printers.

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

#include "libxtide/libxtide.hh"
using namespace libxtide;


// Separator between tide stations in text client output
// \f is form feed
static constString stationSeparator = "\f";


static void doLocation (const Dstr &name,
                        Interval step,
                        FILE *outfp,
                        Mode::Mode mode,
                        Format::Format form) {
  const StationRef *sr (Global::stationIndex().getStationRefByName(name));
  if (sr) {
    std::auto_ptr<Station> station (sr->load());
    station->step = step;

    // Install mark level, if applicable.
    {
      Configurable &cfbl = Global::settings["ml"];
      if (!cfbl.isNull) {
        station->markLevel = cfbl.p;
        if (cfbl.p.Units() != station->predictUnits())
          station->markLevel.Units (station->predictUnits());
      }
    }

    // Find start and end times.
    // This needs to be done here due to the timezones.
    Timestamp startTime, endTime;
    {
      Configurable &cfbl = Global::settings["b"];
      if (cfbl.isNull)
	startTime = (time_t)time(NULL);
      else {
	startTime = Timestamp (cfbl.s, sr->timezone);
	if (startTime.isNull())
	  Global::cant_mktime (cfbl.s, sr->timezone, Error::fatal);
      }
    }{
      Configurable &ecfg = Global::settings["e"];
      if (ecfg.isNull) {
        Configurable &picfg = Global::settings["pi"];
	endTime = startTime + Interval(picfg.u*DAYSECONDS);
      } else {
	endTime = Timestamp (ecfg.s, sr->timezone);
	if (endTime.isNull())
	  Global::cant_mktime (ecfg.s, sr->timezone, Error::fatal);
      }
    }
    if (startTime > endTime) {
      static bool I_told_you_once (false);
      if (!I_told_you_once) {
        fprintf (stderr, "Tide Warning:  swapping begin and end times\n");
        I_told_you_once = true;
      }
      std::swap (startTime, endTime);
    }

    if (mode == Mode::graph && form == Format::PNG)
      station->graphModePNG (outfp, startTime);
    else if (mode == Mode::clock && form == Format::PNG)
      station->clockModePNG (outfp);
    else {
      Dstr text_out;
      station->print (text_out, startTime, endTime, mode, form);
      fprintf (outfp, "%s", text_out.aschar());
    }

  } else {
    Dstr details ("Could not find: ");
    details += name;
    Global::barf (Error::STATION_NOT_FOUND, details, Error::nonfatal);
  }
}


static void loopLocations (FILE *outfp) {
  bool isFirst (true);

  Interval step (Global::hour);
  {
    Configurable &cfbl = Global::settings["s"];
    if (!cfbl.isNull) {
      step = Interval (cfbl.s);
      if (step <= Global::zeroInterval) {
	Dstr details ("You must specify a positive step.  You tried ");
	details += cfbl.s;
	Global::barf (Error::NUMBER_RANGE_ERROR, details);
      }
    }
  }

  Mode::Mode mode;
  Format::Format form;
  {
    Configurable &cfbl = Global::settings["m"];
    if (cfbl.isNull)
      mode = Mode::plain;
    else
      mode = (Mode::Mode)cfbl.c;
  }{
    Configurable &cfbl = Global::settings["f"];
    if (cfbl.isNull)
      form = Format::text;
    else
      form = (Format::Format)cfbl.c;
  }

  if (mode != Mode::list) {
    Configurable &cfbl = Global::settings["l"];
    DstrVector::const_iterator it = cfbl.v.begin();
    DstrVector::const_iterator stop = cfbl.v.end();
    if (it == stop)
      fprintf (stderr, "Warning:  No locations specified with -l; hence, no output.\n");
    while (it != stop) {
      if (isFirst)
	isFirst = false;
      else
	fprintf (outfp, "%s", stationSeparator);
      doLocation (*it, step, outfp, mode, form);
      ++it;
    }
  } else {
    // List mode
    Dstr text_out;
    Global::stationIndex().print (text_out, form);
    fprintf (outfp, "%s", text_out.aschar());
  }
}


int main (int argc, char **argv) {
  srand (time (NULL));
  Global::initTerm();
  Global::settings.applyUserDefaults();
  Global::settings.applyCommandLine (argc, argv);
  Global::settings.fixUpDeprecatedSettings();

  if (!Global::disclaimerDisabled()) {
    fprintf (stderr, "\
-----------------------------------------------------------------------------\n\
            XTide 2   Copyright (C) 1998 David Flater.\n\
\n\
This software is provided under the terms of the GNU General Public\n\
License, either version 3 of the License, or (at your option) any later\n\
version.\n\
\n\
Although the package as a whole is GPL, some individual source files\n\
are public domain.  Consult their header comments for details.\n\
\n\
                        NOT FOR NAVIGATION\n\
\n\
This program is distributed in the hope that it will be useful, but\n\
WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  The author\n\
assumes no liability for damages arising from use of this program OR\n\
of any 'harmonics data' that might be distributed with it.  For details, see\n\
the verbose documentation at http://www.flaterco.com/xtide/.\n\
\n\
This obnoxious message will go away permanently if you click \"Don't show\n\
this again\" in the disclaimer window of the X windows client (xtide), or\n\
if you create a file in your home directory called \".disableXTidedisclaimer\".\n\
-----------------------------------------------------------------------------\n\
\n");
  }

  if (argc < 2 && Global::settings["l"].v.empty()) {
    fprintf (stderr, "\
Minimal usage:  tide -l \"Location name\" (or set the environment variable\n\
  XTIDE_DEFAULT_LOCATION to \"Location name\")\n\n\
Other switches:\n\
  -b \"YYYY-MM-DD HH:MM\"\n\
      Specify the begin (start) time for predictions.\n\
  -e \"YYYY-MM-DD HH:MM\"\n\
      Specify the end (stop) time for predictions.\n\
  -f c|h|i|l|p|t|v\n\
      Specify the output format as CSV, HTML, iCalendar, LaTeX, PNG, text, or\n\
      SVG.  The default is text.\n\
  -m a|b|c|C|g|k|l|m|p|r|s\n\
      Specify mode to be about, banner, calendar, alt. calendar, graph, clock,\n\
      list, medium rare, plain, raw, or stats.  The default is plain.\n\
  -o \"filename\"\n\
      Redirect output to the specified file (appends).\n\
  -s \"HH:MM\"\n\
      Specify the step interval, in hours and minutes, for raw\n\
      mode predictions.  The default is one hour.\n\
  -v\n\
      Print version string and exit.\n\
\n\
These are only the most important switches.  For information on all of\n\
the switches, please read the verbose documentation at:\n\
  http://www.flaterco.com/xtide/\n");
    exit (-1);
  }

  FILE *outfp = stdout;
  {
    Configurable &cfbl = Global::settings["o"];
    if (!cfbl.isNull)
      if (!(outfp = fopen (cfbl.s.aschar(), "ab")))
	Global::cantOpenFile (cfbl.s, Error::fatal);
  }

  #ifdef NO_INITIALIZER_LISTS
  Global::initializeGraphFont();
  #endif

  loopLocations (outfp);

  exit (0);
}

// Cleanup2006 Done
