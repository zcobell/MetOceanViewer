// $Id: Global.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  Global  Global* variables and functions.
    * Within libxtide.

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

class Dstr;

namespace libxtide {

class Settings;
class StationIndex;
class StationRef;
class Interval;


// Non-member operators are declared in the headers for one of the
// respective classes even though they are global in scope.


namespace Global {

  // Constants.

  // Convenience constants to avoid constructing a zillion
  // temporaries.
  extern const Interval zeroInterval;
  extern const Interval hour;
  extern const Interval day;

  // Precision to which events are resolved.  Should be less than
  // eventSafetyMargin.
  extern const Interval eventPrecision;
  // That value in days.
  extern const double eventPrecisionJD;

  // Safety margin to allow when trying to reproduce a tide event
  // given its timestamp.  Should be greater than eventPrecision.
  extern const Interval eventSafetyMargin;
  // One second less than that.
  extern const Interval eventSafetyMarginMinus1;

  // The period of M1C, used in estimation of MLLW.  In NOS CO-OPS 1,
  // the term lunar day is also used, but that term is ambiguous.
  extern const Interval tidalDay;

  // The period of M4, or ¼ of a tidal day.  This is the average time
  // between tide events for a semidiurnal location, used to estimate
  // deltas for obtaining more tide events.
  extern const Interval halfCycle;

  // Cosmological constant for aspect calibration.
  extern const double aspectMagicNumber;

  // Minimum graph width and height.  Also used for clocks.
  extern const unsigned minGraphWidth, minGraphHeight;

  // Minimum TTY width and height.
  extern const unsigned minTTYwidth, minTTYheight;

  // The character used to replace commas in CSV output.
  extern const char CSV_repchar;

  // Year limits for time control dialogs.
  extern const unsigned dialogFirstYear, dialogLastYear;

  // VT100 SCS sequence to assign US-ASCII to G0 and Special Graphics to G1.
  extern constString VT100_init;


  // Variables.

  // The only way that settings can change on the fly is via the xtide
  // control panel.  In tide and xttpd they never change once initialized.
  extern Settings settings;

  // Codeset from locale.
  // - Likely values are ISO-8859-1 and UTF-8.
  // - CP437 (alias *437) is supported for DOS compatibility if Dstr is
  //   version 1.0 or later.
  // - For all other values, XTide reverts to ISO-8859-1 behavior.
  // - ANSI_X3.4-1968 means US-ASCII and sometimes arises on misconfigured
  //   Linux systems.  (Why that and not ANSI_X3.4-1986?)
  // - If nl_langinfo isn't available, the value defaults to ISO-8859-1 but
  //   can be overridden by defining CODESET.
  // - If TERM is vt100 or vt102, codeset will be forced to VT100.  This is
  //   a special case.
  extern Dstr codeset;

  // If TERM is vt100 or vt102, this will be the character sequence to
  // produce a degree sign on such terminals when G1 = Special Graphics.
  // Otherwise, it will be the single Latin-1 character °.
  extern constCharPointer degreeSign;
  inline const bool needDegrees() { return degreeSign[1]; }

  // The user_io_ptr feature of libpng doesn't seem to work.
  // See writePNGToFile.
#ifdef HAVE_PNG_H
  extern FILE *PNGFile;
#endif  

  // Client-side font used in RGBGraph.
  extern ClientSide::Font graphFont;


  // Functions.

  // Initialize codeset from locale.  If TERM is vt100 or vt102, set
  // degreeSign accordingly.
  void initTerm();

  // Perform cumbersome run-time initialization of graphFont when the
  // compiler won't do the compile-time initializer.
  #ifdef NO_INITIALIZER_LISTS
  void initializeGraphFont();
  #endif

  // Apply a codeset translation, if applicable, before output of a Dstr.
  // CP437 is never applied to HTML-format output.
  void finalizeCodeset (Dstr &text_out,
			const Dstr &codeset,
			Format::Format form);

  // Get and set ~/.disableXTidedisclaimer
  const bool disclaimerDisabled();    // If no HOME, default false
  void disableDisclaimer();           // Barfs if HOME is unset

  // Read and retain xtide.conf on first need.
  const Dstr &getXtideConf (unsigned lineNo);

  // Root station index is built on first access.
  StationIndex &stationIndex();
  StationIndex &stationIndex(const char* hfile_path);

  // Don't know where else to put this:  already three different classes
  // need this sanity check, not counting Settings.
  //   emptyInput -- just what it says.  val_out is unchanged.
  //   inputOK    -- value is in val_out.
  //   inputNotOK -- nonfatal error has been issued.  val_out is unchanged.
  enum GetDoubleReturn {emptyInput, inputOK, inputNotOK};
  const GetDoubleReturn getDouble (const Dstr &number,
				   Configurable::Interpretation interpretation,
				   double &val_out);

  // Return true if eventMask is a valid event mask, false if not.
  const bool isValidEventMask (const Dstr &eventMask);

  // Function for libpng.  See also PNGFile.
#ifdef HAVE_PNG_H 
  void writePNGToFile (png_structp png_ptr,
		       png_bytep b_ptr,
		       png_size_t sz);
#endif

  // Rounding functions that round to the nearest integer and round
  // halfway cases toward positive infinity.  Oddly enough, none of
  // the C99 rounding functions will do that.
  // n                    -2½ -2¼ -2  -1¾ -1½ -1¼ -1  -0¾ -0½ -0¼  0   0¼  0½  0¾  1   1¼  1½  1¾  2   2¼  2½
  // cast to int          -2  -2  -2  -1  -1  -1  -1   0   0   0   0   0   0   0   1   1   1   1   2   2   2
  // lrint FE_TONEAREST   -2  -2  -2  -2  -2  -1  -1  -1   0   0   0   0   0   1   1   1   2   2   2   2   2
  // lround               -3  -2  -2  -2  -2  -1  -1  -1  -1   0   0   0   1   1   1   1   2   2   2   2   3
  // Global::iround       -2  -2  -2  -2  -1  -1  -1  -1   0   0   0   0   1   1   1   1   2   2   2   2   3
  const int                   iround (double x);
  const long                  lround (double x);
  const interval_rep_t intervalround (double x);

  // (int) floor(x)
  const int ifloor (double x);


  // Error handling stuff.

  // This function should be called once at the start of the program to
  // divert messages to syslog.
  void setDaemonMode();

  // Install callback for xxErrorBoxes.
  void setErrorCallback (void (*errorCallback) (const Dstr &errorMessage,
						Error::ErrType fatality));

  // Output error message and die (unless Nonfatal specified).
  void barf (Error::TideError err,
             const Dstr &details,
             Error::ErrType fatality = Error::fatal);
  void barf (Error::TideError err,
             Error::ErrType fatality = Error::fatal);

  // Convenience function for mode/format usage.
  void formatBarf (Mode::Mode mode, Format::Format form);

  // Convenience function to add filename and errno message detail for
  // CANT_OPEN_FILE.
  void cantOpenFile (const Dstr &filename, Error::ErrType fatality);

  // Convenience function to add time string and time zone detail for
  // MKTIME_FAILED.
  void cant_mktime (const Dstr &timeString,
		    const Dstr &timezone,
		    Error::ErrType fatality);

  // Other messages to stderr / syslog.
  void log (const Dstr &message, int priority);
  void log (constCharPointer message, int priority);
  // Convenience -- details are just appended to message
  void log (constCharPointer message, const Dstr &details, int priority);

  // Like perror, but messages are diverted through syslog if in daemon mode.
  void xperror (constCharPointer s);
}

}
