// $Id: Global.cc 6184 2016-01-11 01:47:14Z flaterco $

/*  Global  Global variables and functions.

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
#include "HarmonicsPath.hh"
#include <locale.h>
#include <sys/stat.h>
#include <limits>       // No relation to limits.h

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif

namespace libxtide {


// eventPrecisionJD should be static within Skycal, and
// eventSafetyMarginMinus1 should be static within
// TideEventsOrganizer, but they have to be here to avoid the "static
// initialization order fiasco."

const Interval Global::zeroInterval (0);
const Interval Global::hour (HOURSECONDS);
const Interval Global::day (DAYSECONDS);
const Interval Global::eventPrecision (15);
const double Global::eventPrecisionJD (Global::eventPrecision / Global::day);
const Interval Global::eventSafetyMargin (60);
const Interval Global::eventSafetyMarginMinus1 (Global::eventSafetyMargin - 1);
const Interval Global::tidalDay (Global::intervalround (
						360.0*HOURSECONDS/14.4920521));
const Interval Global::halfCycle (Global::intervalround (
						360.0*HOURSECONDS/57.9682084));
const double Global::aspectMagicNumber (56160.0); // Matches XTide 1.
const unsigned Global::minGraphWidth (64U);
const unsigned Global::minGraphHeight (64U);
// If minTTYwidth and minTTYheight are different it will break Banner.
const unsigned Global::minTTYwidth (10U);
const unsigned Global::minTTYheight (10U);
const char Global::CSV_repchar ('|');

// SCS: G0 = US-ASCII, G1 = Special Graphics.
constString Global::VT100_init ("\033(B\033)0");


// Determination of year limits for time control dialogs.

// The current practice of harmonics-dwf is to include years 1700 to
// 2100 because the constituents are calibrated for 1900.

// dialogFirstYear should be 1700 or the earliest full year supported
// by time_t, whichever is later.

// dialogLastYear should be 2100 or the latest full year supported by
// time_t, whichever is earlier.

// Following are the standard requirements on time_t:
// 1.  time_t is an integer or real-floating type.  (SUSv3 sys/types.h)
// 2.  time_t represents time in seconds since the Epoch.  (SUSv3 time())
// 3.  The Epoch is 1970-01-01 00:00 UTC.  (SUSv3 definitions)

// Any real-floating representation should have sufficient range to
// cover 1700 to 2100.  You lose precision if you get too far from the
// Epoch, but the exponent gives you plenty of range.

// N.B., Timestamp.cc assumes that time_t is integral.

// Visual C++, as usual, is a special case:  time_t is int64_t, but
// the library's time functions fail for negative values and for year
// 3000 or later.  The limits calculated below are not used in the
// command-line client, so we don't worry about Visual C++.

// The following is slightly risky in that time_t might use a data
// type that is an extension to the ISO C++ standard, and a conforming
// but stupid implementation might not supply a corresponding
// specialization of numeric_limits.

typedef std::numeric_limits<time_t>  tt_limits;
#define tt_signed    tt_limits::is_signed
#define tt_smallint  tt_limits::is_integer && tt_limits::digits <= 32

const unsigned Global::dialogFirstYear
  (tt_signed ? tt_smallint ? 1902U : 1700U : 1970U);

const unsigned Global::dialogLastYear
  (tt_signed && tt_smallint ? 2037U : 2100U);


Settings Global::settings;
Dstr Global::codeset;
constCharPointer Global::degreeSign ("°");
#ifdef HAVE_PNG_H
FILE *Global::PNGFile = NULL;
#endif

static bool _disclaimerDisabled;
static Dstr disclaimerFileName;
static StationIndex *_stationIndex = NULL;
static bool daemonMode = false;
static void (*_errorCallback) (const Dstr &errorMessage,
                               Error::ErrType fatality) = NULL;

#ifdef HAVE_PNG_H
void Global::writePNGToFile (png_structp png_ptr unusedParameter,
                             png_bytep b_ptr,
                             png_size_t sz) {
  size_t fwriteReturn (fwrite (b_ptr, 1, sz, Global::PNGFile));
  if (fwriteReturn < sz)
    Global::barf (Error::PNG_WRITE_FAILURE);
}
#endif


static void initDisclaimer() {
  static bool disclaimerInit = false;
  if (!disclaimerInit) {
    disclaimerInit = true;
#ifdef UseLocalFiles
    disclaimerFileName = ".disableXTidedisclaimer";
#else
    disclaimerFileName = getenv ("HOME");
    if (disclaimerFileName.isNull()) {
      _disclaimerDisabled = false;
      return;
    }
    disclaimerFileName += "/.disableXTidedisclaimer";
#endif
    struct stat buf;
    _disclaimerDisabled = (stat (disclaimerFileName.aschar(), &buf) == 0);
  }
}


void Global::initTerm() {
  if (codeset.isNull()) {
    const Dstr term (getenv("TERM"));
    if (term == "vt100" || term == "vt102") {
      degreeSign = "\016f\017";  // SO, char, SI.
      codeset = "VT100";
    } else {
#ifdef HAVE_LANGINFO_H
      // Method suggested by man utf-8 to obtain ambient codeset.
      setlocale (LC_CTYPE, "");
      codeset = nl_langinfo (CODESET);

      // Undo damage so that libc functions won't change behavior in
      // mysterious ways.
      setlocale (LC_CTYPE, "C");
#else
#ifdef _MSC_VER
      // Visual C++ land (code by Leonid Tochinski)
      codeset = setlocale (LC_CTYPE, "English_United States.1252");
#else
#ifdef CODESET
      codeset = CODESET;
#else
      codeset = "ISO-8859-1";
#endif
#endif
#endif
    }
  }
  if (codeset.contains("437")) // Catch all aliases
    codeset = "CP437";
  // fprintf (stderr, "Codeset = %s\n", codeset.aschar());
}


void Global::finalizeCodeset (Dstr &text_out,
			      const Dstr &codeset,
			      Format::Format form) {
  if (codeset == "UTF-8")
    text_out.utf8();
#ifdef DSTR_MAJOR_REV
  // CP437 is never applied to HTML-format output.
  else if (codeset == "CP437" && form == Format::text)
    text_out.CP437();
#endif
}


const bool Global::disclaimerDisabled() {
  initDisclaimer();
  return _disclaimerDisabled;
}


void Global::disableDisclaimer() {
  initDisclaimer();
  if (disclaimerFileName.isNull())
    Global::barf (Error::NOHOMEDIR);
  FILE *fp = fopen (disclaimerFileName.aschar(), "wb");
  if (!fp) {
    cantOpenFile (disclaimerFileName, Error::nonfatal);
  } else {
    fclose (fp);
    _disclaimerDisabled = true;
  }
}


// Read and retain xtide.conf on first need.  assertfail on lineNo > 1.
const Dstr &Global::getXtideConf (unsigned lineNo) {
#ifdef UseLocalFiles
  static const char confFile[] = "xtide.conf";
#else
  static const char confFile[] = "/etc/xtide.conf";
#endif
  static bool firstCall (true);
  static SafeVector<Dstr> xtideConfLines (2U);
  if (firstCall) {
    firstCall = false;
    FILE *configFile;
    if ((configFile = fopen (confFile, "rb"))) {
      for (unsigned i=0; i<2; ++i) {
        Dstr &line (xtideConfLines[i]);
        line.getline (configFile);
        // Kluge for MS-DOS line discipline
        if (line.back() == '\r')
          line -= line.length() - 1;
      }
      fclose (configFile);
    }
  }
  return xtideConfLines[lineNo];
}


StationIndex &Global::stationIndex () {
  if (!_stationIndex) {
    Dstr unparsedHfilePath (getenv ("HFILE_PATH"));
    if (unparsedHfilePath.isNull())
      unparsedHfilePath = getXtideConf(0U);
    HarmonicsPath harmonicsPath (unparsedHfilePath);
    _stationIndex = new StationIndex();
    for (unsigned i=0; i<harmonicsPath.size(); ++i) {
      struct stat s;
      if (stat (harmonicsPath[i].aschar(), &s) == 0) {
#ifdef HAVE_DIRENT_H
        if (S_ISDIR (s.st_mode)) {
          Dstr dname (harmonicsPath[i]);
          dname += '/';
          DIR *dirp = opendir (dname.aschar());
          if (!dirp)
            xperror (dname.aschar());
          else {
            dirent *dp;
	    for (dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
              Dstr fname (dp->d_name);
	      if (fname[0] == '.') // Skip all hidden files
		continue;
	      else {
                fname *= dname;
                _stationIndex->addHarmonicsFile (fname);
              }
	    }
	    closedir(dirp);
          }
#else
	// Visual C++ land (code by Leonid Tochinski)
        if (_S_IFDIR & s.st_mode) {
          Dstr dname (harmonicsPath[i]);
          if (dname.back() != '\\')
             dname += '\\';
          _finddata_t fileinfo;
          memset(&fileinfo, 0 ,sizeof fileinfo);
          Dstr mask (dname);
          mask += "*.tcd";
          intptr_t findptr = _findfirst(mask.aschar(), &fileinfo);
          if (-1 != findptr) {
            do {
               Dstr fname (fileinfo.name);
               if (fname[0] == '.') // Skip all hidden files
                 continue;
               else {
                 fname *= dname;
                 _stationIndex->addHarmonicsFile (fname);
               }
            } while (-1 != _findnext(findptr,&fileinfo));
            _findclose(findptr);
          }
#endif
        } else
          _stationIndex->addHarmonicsFile (harmonicsPath[i]);
      } else
        xperror (harmonicsPath[i].aschar());
    }
    if (_stationIndex->empty()) {
      if (harmonicsPath.noPathProvided())
        Global::barf (Error::NO_HFILE_PATH);
      else
	Global::barf (Error::NO_HFILE_IN_PATH, harmonicsPath.origPath());
      // Ignore the stupid case where the file exists but contains no
      // stations.
    }
    _stationIndex->sort();
    _stationIndex->setRootStationIndexIndices();
  }
  return *_stationIndex;
}

StationIndex &Global::stationIndex (const char* hfile_path) {
  if (!_stationIndex) {
    Dstr unparsedHfilePath = hfile_path;
    if (unparsedHfilePath.isNull())
      unparsedHfilePath = getXtideConf(0U);
    HarmonicsPath harmonicsPath (unparsedHfilePath);
    _stationIndex = new StationIndex();
    for (unsigned i=0; i<harmonicsPath.size(); ++i) {
      struct stat s;
      if (stat (harmonicsPath[i].aschar(), &s) == 0) {
#ifdef HAVE_DIRENT_H
        if (S_ISDIR (s.st_mode)) {
          Dstr dname (harmonicsPath[i]);
          dname += '/';
          DIR *dirp = opendir (dname.aschar());
          if (!dirp)
            xperror (dname.aschar());
          else {
            dirent *dp;
	    for (dp = readdir(dirp); dp != NULL; dp = readdir(dirp)) {
              Dstr fname (dp->d_name);
	      if (fname[0] == '.') // Skip all hidden files
		continue;
	      else {
                fname *= dname;
                _stationIndex->addHarmonicsFile (fname);
              }
	    }
	    closedir(dirp);
          }
#else
	// Visual C++ land (code by Leonid Tochinski)
        if (_S_IFDIR & s.st_mode) {
          Dstr dname (harmonicsPath[i]);
          if (dname.back() != '\\')
             dname += '\\';
          _finddata_t fileinfo;
          memset(&fileinfo, 0 ,sizeof fileinfo);
          Dstr mask (dname);
          mask += "*.tcd";
          intptr_t findptr = _findfirst(mask.aschar(), &fileinfo);
          if (-1 != findptr) {
            do {
               Dstr fname (fileinfo.name);
               if (fname[0] == '.') // Skip all hidden files
                 continue;
               else {
                 fname *= dname;
                 _stationIndex->addHarmonicsFile (fname);
               }
            } while (-1 != _findnext(findptr,&fileinfo));
            _findclose(findptr);
          }
#endif
        } else
          _stationIndex->addHarmonicsFile (harmonicsPath[i]);
      } else
        xperror (harmonicsPath[i].aschar());
    }
    if (_stationIndex->empty()) {
      if (harmonicsPath.noPathProvided())
        Global::barf (Error::NO_HFILE_PATH);
      else
	Global::barf (Error::NO_HFILE_IN_PATH, harmonicsPath.origPath());
      // Ignore the stupid case where the file exists but contains no
      // stations.
    }
    _stationIndex->sort();
    _stationIndex->setRootStationIndexIndices();
  }
  return *_stationIndex;
}


void Global::setDaemonMode() {
#ifdef HAVE_SYSLOG_H
  daemonMode = true;
  openlog ("xttpd", LOG_CONS|LOG_PID, LOG_DAEMON);
#else
  Global::barf (Error::NO_SYSLOG);
#endif
}


void Global::setErrorCallback (void (*errorCallback) (const Dstr &errorMessage,
						    Error::ErrType fatality)) {
  _errorCallback = errorCallback;
}


void Global::xperror (constCharPointer s) {
#ifdef HAVE_SYSLOG_H
  if (daemonMode)
    syslog (LOG_ERR, "%s: %s", s, strerror (errno));
  else
#endif
    perror (s);
}


void Global::log (const Dstr &message, int priority) {
  log (message.aschar(), priority);
}


void Global::log (constCharPointer message, int priority) {
  if (message) {
#ifdef HAVE_SYSLOG_H
    if (daemonMode)
      syslog (priority, "%s", message);
    else
#endif
      fprintf (stderr, "%s\n", message);
  }
}


void Global::log (constCharPointer message,
                  const Dstr &details,
                  int priority) {
  Dstr temp (message);
  temp += details;
  log (temp, priority);
}


static void errorMessage (Error::TideError err,
                          const Dstr &details,
		          Error::ErrType fatality,
                          Dstr &text_out) {
  if (fatality == Error::fatal)
    text_out = "XTide Fatal Error:  ";
  else
    text_out = "XTide Error:  ";
  switch (err) {
  case Error::YEAR_OUT_OF_RANGE:
    text_out += "YEAR_OUT_OF_RANGE\n\
Some operation has attempted to access time before 1 A.D. or after\n\
4000 A.D.  There's no point even trying to predict tides over that\n\
span of time.";
    break;
  case Error::MKTIME_FAILED:
    text_out += "MKTIME_FAILED\n\
XTide was unable to convert an informal time specification into a\n\
legal Unix timestamp.  This probably means that there is something\n\
wrong with the informal time specification, such as referring to a\n\
point in time that does not exist (like February 30th).  It may also\n\
mean that the requested time is outside of the Unix epoch, which only\n\
reliably includes the years from 1970 through 2037.  For the workaround,\n\
see http://www.flaterco.com/xtide/time_t.html.";
    break;
  case Error::TIMESTAMP_OVERFLOW:
    text_out += "TIMESTAMP_OVERFLOW\n\
A timestamp operation resulted in a value that was outside of the\n\
supported epoch.  The Unix epoch only reliably includes the years from\n\
1970 through 2037.  For the workaround, see\n\
http://www.flaterco.com/xtide/time_t.html.";
    break;
  case Error::YEAR_NOT_IN_TABLE:
    text_out += "YEAR_NOT_IN_TABLE\n\
Some operation has been initiated that needs data for a year that is\n\
not supported by the harmonics file being used.";
    break;
  case Error::NO_HFILE_PATH:
    text_out += "NO_HFILE_PATH\n\
If /etc/xtide.conf is not provided, you must set the environment variable\n\
HFILE_PATH to point to your harmonics files.  Example:\n\
  export HFILE_PATH=/usr/local/share/xtide/harmonics.tcd\n\
Please refer to the documentation for usage of /etc/xtide.conf.";
    break;
  case Error::NO_HFILE_IN_PATH:
    text_out += "NO_HFILE_IN_PATH\n\
A search path for harmonics files was provided, but no harmonics files were\n\
found anywhere in that path.  You might need to download a harmonics file\n\
from http://www.flaterco.com/xtide/files.html.";
    break;
  case Error::IMPOSSIBLE_CONVERSION:
    text_out += "IMPOSSIBLE_CONVERSION\n\
An attempt was made to convert between units of fundamentally different types,\n\
e.g., to convert from units of velocity to units of length.";
    break;
  case Error::NO_CONVERSION:
    text_out += "NO_CONVERSION\n\
An attempt was made to convert a measure with units to the same units.  This\n\
may indicate a fault in the program logic.";
    break;
  case Error::UNRECOGNIZED_UNITS:
    text_out += "UNRECOGNIZED_UNITS\n\
The units of a prediction value (e.g., feet, meters, knots) were not one of\n\
the recognized alternatives.";
    break;
  case Error::BOGUS_COORDINATES:
    text_out += "BOGUS_COORDINATES\n\
A latitude and longitude pair was found to be out of range.";
    break;
  case Error::CANT_OPEN_FILE:
    text_out += "CANT_OPEN_FILE\n\
Unable to open a file.";
    break;
  case Error::CORRUPT_HARMONICS_FILE:
    text_out += "CORRUPT_HARMONICS_FILE\n\
Your harmonics file does not conform to the required format.  This can be\n\
caused by:\n\
   --  updates to the harmonics file while xtide is running;\n\
   --  a corrupt harmonics file;\n\
   --  libtcd errors (which would have been reported to stderr).\n\
If the harmonics file has been updated, just restart xtide and it will be fine.";
    break;
  case Error::BADCOLORSPEC:
    text_out += "BADCOLORSPEC\n\
A color specification could not be parsed.";
    break;
  case Error::XPM_ERROR:
    text_out += "XPM_ERROR\n\
An error condition was reported by an Xpm library function.";
    break;
  case Error::NOHOMEDIR:
    text_out += "NOHOMEDIR\n\
The environment variable HOME is not set.";
    break;
  case Error::XMLPARSE:
    text_out += "XMLPARSE\n\
The XML file is ill-formed or exceeds the limitations of XTide's parser.";
    break;
  case Error::STATION_NOT_FOUND:
    text_out += "STATION_NOT_FOUND\n\
The specified station was not found in any harmonics file.";
    break;
  case Error::BADHHMM:
    text_out += "BADHHMM\n\
XTide was expecting an interval specification of the form [-]HH:MM where HH\n\
is hours and MM is minutes.  What it got did not parse.  This indicates a\n\
problem with an offset, meridian, or step value specification.";
    break;
  case Error::CANTOPENDISPLAY:
    text_out += "CANTOPENDISPLAY\n\
XTide cannot open your X11 display.  Check the setting of the\n\
DISPLAY environment variable, and check your permissions (xhost,\n\
xauth, kerberos, firewall, etc.).";
    break;
  case Error::NOT_A_NUMBER:
    text_out += "NOT_A_NUMBER\n\
Couldn't convert a text string to a number.";
    break;
  case Error::PNG_WRITE_FAILURE:
    text_out += "PNG_WRITE_FAILURE\n\
A general fatal error occurred while producing a PNG.";
    break;
  case Error::CANT_GET_SOCKET:
    text_out += "CANT_GET_SOCKET\n\
Xttpd was unable to bind its socket.  Common causes are (1) you tried to\n\
use the default port 80 without having root privileges; fix this by\n\
providing a usable port number (e.g., 8080) as the first command-line\n\
argument, or (2) there is already something running on the specified port,\n\
such as another web server.";
    break;
  case Error::SYSTEMD_NO_SOCKET:
    text_out += "SYSTEMD_NO_SOCKET\n\
Xttpd did not receive a socket from systemd as expected.";
    break;
  case Error::CANT_GET_HOST:
    text_out += "CANT_GET_HOST\n\
Xttpd was unable to determine the official name of the host machine.  The\n\
gethostbyname function returned failure when asked to resolve the node name.";
    break;
  case Error::ABSURD_OFFSETS:
    text_out += "ABSURD_OFFSETS\n\
A subordinate station's offsets were so nonsensical as to cause\n\
operational failures in XTide.";
    break;
  case Error::NUMBER_RANGE_ERROR:
    text_out += "NUMBER_RANGE_ERROR\n\
A number was parsed OK, but it is not in the range of acceptable values.";
    break;
  case Error::BAD_MODE:
    text_out += "BAD_MODE\n\
A mode specified with the -m command line switch is not supported.";
    break;
  case Error::BAD_FORMAT:
    text_out += "BAD_FORMAT\n\
The format requested with the -f command line switch is not supported for\n\
the selected mode.  The currently supported combinations of mode and format\n\
are as folows:\n\
\n\
	 MODE\n\
      abcCgklmprs\n\
  F c   X    XXX\n\
  O h X XX  X\n\
  R i   X\n\
  M l   XX\n\
  A p     XX\n\
  T t XXXXXXXXXXX\n\
    v     XX";
    break;
  case Error::BAD_GRAPHSTYLE:
    text_out += "BAD_GRAPHSTYLE\n\
A specified graph style is not supported.";
    break;
  case Error::BAD_EVENTMASK:
    text_out += "BAD_EVENTMASK\n\
An eventmask may contain any combination of the letters p (phase of moon),\n\
S (sunrise), s (sunset), M (moonrise), and m (moonset), or it may be x\n\
(suppress none).  An eventmask was specified that did not conform to that.";
    break;
  case Error::BAD_TIMESTAMP:
    text_out += "BAD_TIMESTAMP\n\
The -b and -e command line switches expect timestamps to be in the format\n\
\"YYYY-MM-DD HH:MM\".  Example:  tide -b \"1998-01-01 13:00\"";
    break;
  case Error::BAD_BOOL:
    text_out += "BAD_BOOL\n\
A boolean parameter was set to some value other than 'y' or 'n'.";
    break;
  case Error::BAD_TEXT:
    text_out += "BAD_TEXT\n\
A text parameter had something not right about it.";
    break;
  case Error::BAD_OR_AMBIGUOUS_COMMAND_LINE:
    text_out += "BAD_OR_AMBIGUOUS_COMMAND_LINE\n\
The command line could not be rationalized.  Probably you have provided an\n\
unrecognized switch or invalid argument.  However, you can also get this\n\
error by using ambiguous syntax.  For example, the shorthand -lw5 could mean\n\
\"set the line width to 5\" (-lw 5) or it could mean \"load the location\n\
named w5\" (-l w5).";
    break;
  case Error::CANT_LOAD_FONT:
    text_out += "CANT_LOAD_FONT\n\
A font that is required by XTide does not appear on the system.\n\
Perhaps there is an optional font package that you need to install.";
    break;
  case Error::NO_SYSLOG:
    text_out += "NO_SYSLOG\n\
This platform has no syslog facility, so xttpd cannot run as a daemon.";
    break;
  case Error::BROKEN_ZONEINFO:
    text_out += "BROKEN_ZONEINFO\n"
#ifdef DJGPP
"Your platform appears to have broken time zone support.  You need to get the\n\
DJGPP zoneinfo package and unpack it in C:\\DJGPP.  Note that version 2.03\n\
still does Daylight Savings Time wrong for the U.S. (it predates the changes\n\
that took effect in 2007).";
#else
"Your platform appears to have broken time zone support.  You need to have a\n\
reasonably modern version of the time zone database that is available from\n\
http://www.iana.org/time-zones.";
#endif
    break;
  case Error::ADDRESS_FAIL:
    text_out += "ADDRESS_FAIL\n\
Xttpd attempted to parse an address from the command line, but it failed.";
    break;
  case Error::PORT_FAIL:
    text_out += "PORT_FAIL\n\
Xttpd attempted to parse a port number from the command line, but it failed.";
    break;
  default:
    assert (false);
  }
  text_out += '\n';
  if (!(details.isNull())) {
    text_out += "\nError details:\n";
    text_out += details;
    text_out += '\n';
  }
}


void Global::barf (Error::TideError err,
                   const Dstr &details,
                   Error::ErrType fatality) {
  static bool snakeBit (false); // Hide double-barfs.
  Dstr message;
  errorMessage (err, details, fatality, message);
  if (!snakeBit)
    log (message, LOG_ERR);
  if (fatality == Error::fatal)
    snakeBit = true;
  if (_errorCallback)
    (*_errorCallback) (message, fatality);
  if (fatality == Error::fatal)
    exit (-1);
}


void Global::barf (Error::TideError err, Error::ErrType fatality) {
  barf (err, Dstr(), fatality);
}


void Global::formatBarf (Mode::Mode mode, Format::Format form) {
  Dstr details ("Can't do format ");
  details += (char)form;
  details += " in mode ";
  details += (char)mode;
  details += '.';
  barf (Error::BAD_FORMAT, details);
}


const Global::GetDoubleReturn Global::getDouble (const Dstr &number,
                                   Configurable::Interpretation interpretation,
                                                 double &val_out) {
  assert (interpretation == Configurable::posDoubleInterp ||
          interpretation == Configurable::nonnegativeDoubleInterp ||
          interpretation == Configurable::opacityDoubleInterp ||
          interpretation == Configurable::numberInterp);
  if (number.length() > 0) {
    if (number.strchr ('\n') != -1 ||
        number.strchr ('\r') != -1 ||
        number.strchr (' ') != -1) {
      Dstr details ("Numbers aren't supposed to contain whitespace.  You entered '");
      details += number;
      details += "'.";
      barf (Error::NOT_A_NUMBER, details, Error::nonfatal);
      return inputNotOK;
    } else {
      double temp;
      if (sscanf (number.aschar(), "%lf", &temp) != 1) {
	Dstr details ("The offending input was '");
	details += number;
	details += "'.";
	barf (Error::NOT_A_NUMBER, details, Error::nonfatal);
        return inputNotOK;
      } else {
        if (
        (temp  < 0.0 && interpretation != Configurable::numberInterp) ||
        (temp == 0.0 && interpretation == Configurable::posDoubleInterp) ||
        (temp  > 1.0 && interpretation == Configurable::opacityDoubleInterp)) {
  	  Dstr details ("The offending input was '");
	  details += number;
	  details += "'.";
	  barf (Error::NUMBER_RANGE_ERROR, details, Error::nonfatal);
          return inputNotOK;
        } else {
          val_out = temp;
  	  return inputOK;
        }
      }
    }
  }
  return emptyInput;
}


const bool Global::isValidEventMask (const Dstr &eventMask) {
  static constString legalEventMasks = "pSsMm";
  if (eventMask.length() < 1)
    return false;
  if (eventMask == "x")
    return true;
  for (unsigned i=0; i<eventMask.length(); ++i)
    if (!strchr (legalEventMasks, eventMask[i]))
      return false;
  return true;
}


void Global::cantOpenFile (const Dstr &filename,
                           Error::ErrType fatality) {
  Dstr details (filename);
  details += ": ";
  details += strerror (errno);
  details += '.';
  barf (Error::CANT_OPEN_FILE, details, fatality);
}


void Global::cant_mktime (const Dstr &timeString,
                          const Dstr &timezone,
                          Error::ErrType fatality) {
  Dstr details ("The offending input was ");
  details += timeString;
  details += "\nin the time zone ";
  if (settings["z"].c == 'n')
    details += timezone;
  else
    details += "UTC0";
  barf (Error::MKTIME_FAILED, details, fatality);
}


const int Global::iround (double x) {
  return (int) floor (x+0.5);
}


const long Global::lround (double x) {
  return (long) floor (x+0.5);
}


const interval_rep_t Global::intervalround (double x) {
  return (interval_rep_t) floor (x+0.5);
}


const int Global::ifloor (double x) {
  return (int) floor (x);
}


// The initial value of Global::graphFont is a scrape of the first 256 glyphs
// of LiberationSans-10 as rendered by libXft 2.3.1 under the following
// conditions:
//
//   NVIDIA(0): Computing DPI using physical size from ACI VK246 (DFP-0)'s
//   NVIDIA(0):     EDID and first mode to be programmed on ACI VK246
//   NVIDIA(0):     (DFP-0):
//   NVIDIA(0):   width  : 1920 pixels  530  mm (DPI: 92)
//   NVIDIA(0):   height : 1080 pixels  300  mm (DPI: 91)
//   NVIDIA(0): DPI set to (92, 91); computed from "UseEdidDpi" X config
//   NVIDIA(0):     option
//   Depth 24 pixmap format is 32 bpp
//
// This embedding avoids the need for tide or xttpd to be dependent on
// libraries that typically are installed only when X11 is present.
/*
LICENSE AGREEMENT AND LIMITED PRODUCT WARRANTY
LIBERATION FONT SOFTWARE

This agreement governs the use of the Software and any updates to the Software, regardless of the delivery mechanism. Subject to the following terms, Red Hat, Inc. ("Red Hat") grants to the user ("Client") a license to this work pursuant to the GNU General Public License v.2 with the exceptions set forth below and such other terms as are set forth in this End User License Agreement.

 1. The Software and License Exception. LIBERATION font software  (the "Software") consists of TrueType-OpenType formatted font software for rendering LIBERATION typefaces in sans-serif, serif, and monospaced character styles. You are licensed to use, modify, copy, and distribute the Software pursuant to the GNU General Public License v.2 with the following exceptions:  

  (a) As a special exception, if you create a document which uses this font, and embed this font or unaltered portions of this font into the document, this font does not by itself cause the resulting document to be covered by the GNU General Public License. This exception does not however invalidate any other reasons why the document might be covered by the GNU General Public License. If you modify this font, you may extend this exception to your version of the font, but you are not obligated to do so. If you do not wish to do so, delete this exception statement from your version.

  (b) As a further exception, any distribution of the object code of the Software in a physical product must provide you the right to access and modify the source code for the Software and to reinstall that modified version of the Software in object code form on the same physical product on which you received it.

 2. Intellectual Property Rights. The Software and each of its components, including the source code, documentation, appearance, structure and organization are owned by Red Hat and others and are protected under copyright and other laws. Title to the Software and any component, or to any copy, modification, or merged portion shall remain with the aforementioned, subject to the applicable license. The "LIBERATION" trademark is a trademark of Red Hat, Inc. in the U.S. and other countries. This agreement does not permit Client to distribute modified versions of the Software using Red Hat's trademarks. If Client makes a redistribution of a modified version of the Software, then Client must modify the files names to remove any reference to the Red Hat trademarks and must not use the Red Hat trademarks in any way to reference or promote the modified Software. 

 3. Limited Warranty. To the maximum extent permitted under applicable law, the Software is provided and licensed "as is" without warranty of any kind, expressed or implied, including the implied warranties of merchantability, non-infringement or fitness for a particular purpose. Red Hat does not warrant that the functions contained in the Software will meet Client's requirements or that the operation of the Software will be entirely error free or appear precisely as described in the accompanying documentation. 

 4. Limitation of Remedies and Liability. To the maximum extent permitted by applicable law, Red Hat or any Red Hat authorized dealer will not be liable to Client for any incidental or consequential damages, including lost profits or lost savings arising out of the use or inability to use the Software, even if Red Hat or such dealer has been advised of the possibility of such damages. 

 5. General. If any provision of this agreement is held to be unenforceable, that shall not affect the enforceability of the remaining provisions. This agreement shall be governed by the laws of the State of North Carolina and of the United States, without regard to any conflict of laws provisions, except that the United Nations Convention on the International Sale of Goods shall not apply.
Copyright © 2007-2011 Red Hat, Inc. All rights reserved. LIBERATION is a trademark of Red Hat, Inc.
*/

#ifndef NO_INITIALIZER_LISTS

// Workaround for g++ 4.4 series.
#ifdef BROKEN_INITIALIZER_LISTS
#define G (ClientSide::Glyph)
#define P (ClientSide::Pixel)
#else
#define G
#define P
#endif

ClientSide::Font Global::graphFont (
 {12,9,{
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{4,{}},
  G{3,{P{1,3,251},P{1,4,242},P{1,5,233},P{1,6,224},P{1,7,215},P{1,8,206},P{1,9,196},P{1,11,255}}},
  G{5,{P{1,3,246},P{3,3,246},P{1,4,226},P{3,4,226},P{1,5,206},P{3,5,206}}},
  G{7,{P{2,3,152},P{3,3,125},P{5,3,151},P{6,3,124},P{2,4,144},P{3,4,71},P{5,4,141},P{6,4,68},P{0,5,108},P{1,5,255},P{2,5,255},P{3,5,255},P{4,5,255},P{5,5,255},P{6,5,255},P{7,5,96},P{1,6,6},P{2,6,201},P{4,6,5},P{5,6,195},P{1,7,84},P{2,7,165},P{4,7,80},P{5,7,163},P{1,8,180},P{2,8,111},P{4,8,176},P{5,8,110},P{0,9,188},P{1,9,255},P{2,9,255},P{3,9,255},P{4,9,255},P{5,9,255},P{6,9,255},P{7,9,12},P{1,10,172},P{2,10,7},P{4,10,169},P{5,10,9},P{1,11,180},P{4,11,177}}},
  G{7,{P{3,2,255},P{1,3,74},P{2,3,226},P{3,3,255},P{4,3,225},P{5,3,64},P{1,4,223},P{2,4,66},P{3,4,255},P{4,4,81},P{5,4,212},P{1,5,244},P{2,5,11},P{3,5,255},P{4,5,1},P{5,5,37},P{1,6,178},P{2,6,137},P{3,6,255},P{4,6,4},P{1,7,15},P{2,7,140},P{3,7,255},P{4,7,227},P{5,7,63},P{3,8,255},P{4,8,83},P{5,8,215},P{1,9,99},P{2,9,5},P{3,9,255},P{4,9,7},P{5,9,248},P{1,10,202},P{2,10,89},P{3,10,255},P{4,10,79},P{5,10,209},P{1,11,57},P{2,11,221},P{3,11,255},P{4,11,219},P{5,11,60},P{3,12,255}}},
  G{12,{P{1,3,82},P{2,3,231},P{3,3,233},P{4,3,80},P{7,3,25},P{8,3,213},P{9,3,13},P{1,4,224},P{2,4,63},P{3,4,61},P{4,4,223},P{7,4,167},P{8,4,84},P{1,5,249},P{2,5,5},P{3,5,6},P{4,5,249},P{6,5,68},P{7,5,183},P{1,6,216},P{2,6,64},P{3,6,68},P{4,6,215},P{5,6,7},P{6,6,209},P{7,6,35},P{1,7,78},P{2,7,233},P{3,7,232},P{4,7,76},P{5,7,126},P{6,7,125},P{7,7,85},P{8,7,233},P{9,7,232},P{10,7,82},P{4,8,35},P{5,8,209},P{6,8,7},P{7,8,226},P{8,8,63},P{9,8,61},P{10,8,223},P{4,9,183},P{5,9,68},P{7,9,250},P{8,9,5},P{9,9,6},P{10,9,249},P{3,10,85},P{4,10,166},P{7,10,218},P{8,10,64},P{9,10,68},P{10,10,213},P{2,11,13},P{3,11,214},P{4,11,24},P{7,11,81},P{8,11,233},P{9,11,231},P{10,11,73}}},
  G{9,{P{2,3,92},P{3,3,231},P{4,3,236},P{5,3,67},P{2,4,236},P{3,4,63},P{4,4,55},P{5,4,228},P{2,5,241},P{3,5,11},P{4,5,38},P{5,5,205},P{2,6,179},P{3,6,136},P{4,6,177},P{5,6,40},P{1,7,68},P{2,7,226},P{3,7,236},P{4,7,10},P{6,7,13},P{7,7,142},P{1,8,207},P{2,8,57},P{3,8,188},P{4,8,127},P{6,8,115},P{7,8,130},P{1,9,247},P{2,9,9},P{3,9,41},P{4,9,243},P{5,9,126},P{6,9,206},P{7,9,12},P{1,10,209},P{2,10,102},P{3,10,11},P{4,10,173},P{5,10,255},P{6,10,102},P{7,10,8},P{1,11,57},P{2,11,212},P{3,11,247},P{4,11,176},P{5,11,102},P{6,11,223},P{7,11,244}}},
  G{2,{P{1,3,246},P{1,4,226},P{1,5,206}}},
  G{4,{P{2,3,49},P{3,3,206},P{4,3,10},P{1,4,3},P{2,4,212},P{3,4,48},P{1,5,88},P{2,5,174},P{1,6,171},P{2,6,88},P{1,7,225},P{2,7,31},P{1,8,247},P{2,8,9},P{1,9,247},P{2,9,9},P{1,10,225},P{2,10,31},P{1,11,171},P{2,11,87},P{1,12,88},P{2,12,172},P{1,13,3},P{2,13,213},P{3,13,46},P{2,14,50},P{3,14,205},P{4,14,10}}},
  G{4,{P{-1,3,11},P{0,3,208},P{1,3,47},P{0,4,51},P{1,4,210},P{2,4,2},P{1,5,178},P{2,5,83},P{1,6,93},P{2,6,168},P{1,7,35},P{2,7,225},P{1,8,11},P{2,8,246},P{1,9,11},P{2,9,246},P{1,10,35},P{2,10,225},P{1,11,92},P{2,11,168},P{1,12,176},P{2,12,84},P{0,13,49},P{1,13,211},P{2,13,2},P{-1,14,10},P{0,14,206},P{1,14,48}}},
  G{5,{P{2,3,244},P{0,4,113},P{1,4,45},P{2,4,221},P{3,4,49},P{4,4,113},P{0,5,97},P{1,5,183},P{2,5,254},P{3,5,177},P{4,5,95},P{0,6,11},P{1,6,193},P{2,6,127},P{3,6,167},P{4,6,1},P{0,7,111},P{1,7,105},P{3,7,95},P{4,7,73}}},
  G{8,{P{3,4,184},P{3,5,255},P{3,6,255},P{0,7,176},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,176},P{3,8,255},P{3,9,255},P{3,10,184}}},
  G{4,{P{2,11,255},P{1,12,25},P{2,12,240},P{1,13,165},P{2,13,97}}},
  G{4,{P{1,8,255},P{2,8,255}}},
  G{4,{P{2,11,255}}},
  G{4,{P{2,3,12},P{3,3,200},P{2,4,99},P{3,4,125},P{2,5,196},P{3,5,39},P{1,6,39},P{2,6,207},P{1,7,136},P{2,7,121},P{0,8,3},P{1,8,229},P{2,8,35},P{0,9,76},P{1,9,204},P{0,10,174},P{1,10,117},P{-1,11,21},P{0,11,249},P{1,11,31}}},
  G{7,{P{1,3,16},P{2,3,183},P{3,3,241},P{4,3,183},P{5,3,15},P{1,4,137},P{2,4,121},P{3,4,14},P{4,4,120},P{5,4,134},P{1,5,213},P{2,5,37},P{4,5,39},P{5,5,209},P{1,6,245},P{2,6,9},P{4,6,10},P{5,6,243},P{1,7,253},P{2,7,1},P{4,7,2},P{5,7,253},P{1,8,244},P{2,8,9},P{4,8,11},P{5,8,242},P{1,9,210},P{2,9,38},P{4,9,41},P{5,9,206},P{1,10,132},P{2,10,123},P{3,10,14},P{4,10,126},P{5,10,129},P{1,11,14},P{2,11,183},P{3,11,241},P{4,11,179},P{5,11,12}}},
  G{7,{P{2,3,88},P{3,3,254},P{1,4,59},P{2,4,134},P{3,4,255},P{1,5,113},P{3,5,255},P{3,6,255},P{3,7,255},P{3,8,255},P{3,9,255},P{3,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255}}},
  G{7,{P{1,3,33},P{2,3,191},P{3,3,244},P{4,3,209},P{5,3,55},P{1,4,190},P{2,4,122},P{3,4,10},P{4,4,101},P{5,4,208},P{1,5,98},P{2,5,7},P{4,5,13},P{5,5,244},P{4,6,81},P{5,6,188},P{3,7,40},P{4,7,224},P{5,7,47},P{2,8,45},P{3,8,224},P{4,8,73},P{1,9,26},P{2,9,225},P{3,9,75},P{1,10,165},P{2,10,131},P{1,11,254},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,36}}},
  G{7,{P{1,3,38},P{2,3,196},P{3,3,245},P{4,3,206},P{5,3,53},P{1,4,193},P{2,4,115},P{3,4,10},P{4,4,103},P{5,4,210},P{1,5,92},P{2,5,5},P{4,5,11},P{5,5,246},P{3,6,4},P{4,6,107},P{5,6,180},P{3,7,255},P{4,7,228},P{5,7,36},P{3,8,4},P{4,8,106},P{5,8,194},P{1,9,117},P{2,9,7},P{4,9,11},P{5,9,246},P{1,10,194},P{2,10,119},P{3,10,10},P{4,10,104},P{5,10,204},P{1,11,39},P{2,11,198},P{3,11,245},P{4,11,208},P{5,11,53}}},
  G{7,{P{3,3,92},P{4,3,255},P{2,4,1},P{3,4,190},P{4,4,255},P{2,5,78},P{3,5,137},P{4,5,255},P{2,6,197},P{3,6,33},P{4,6,255},P{1,7,64},P{2,7,180},P{4,7,255},P{1,8,184},P{2,8,72},P{4,8,255},P{1,9,255},P{2,9,255},P{3,9,255},P{4,9,255},P{5,9,255},P{6,9,192},P{4,10,255},P{4,11,255}}},
  G{7,{P{1,3,188},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,148},P{1,4,205},P{2,4,35},P{1,5,221},P{2,5,17},P{1,6,237},P{2,6,185},P{3,6,242},P{4,6,191},P{5,6,30},P{1,7,164},P{2,7,65},P{3,7,13},P{4,7,143},P{5,7,177},P{4,8,18},P{5,8,240},P{1,9,49},P{2,9,2},P{4,9,20},P{5,9,233},P{1,10,203},P{2,10,108},P{3,10,12},P{4,10,141},P{5,10,166},P{1,11,49},P{2,11,208},P{3,11,244},P{4,11,185},P{5,11,25}}},
  G{7,{P{1,3,2},P{2,3,149},P{3,3,244},P{4,3,221},P{5,3,65},P{1,4,104},P{2,4,138},P{3,4,17},P{4,4,86},P{5,4,199},P{1,5,194},P{2,5,45},P{1,6,238},P{2,6,87},P{3,6,233},P{4,6,207},P{5,6,40},P{1,7,252},P{2,7,133},P{3,7,13},P{4,7,129},P{5,7,186},P{1,8,239},P{2,8,38},P{4,8,15},P{5,8,241},P{1,9,208},P{2,9,51},P{4,9,16},P{5,9,237},P{1,10,125},P{2,10,142},P{3,10,15},P{4,10,128},P{5,10,172},P{1,11,11},P{2,11,178},P{3,11,246},P{4,11,195},P{5,11,30}}},
  G{7,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{4,4,104},P{5,4,169},P{3,5,18},P{4,5,223},P{5,5,26},P{3,6,131},P{4,6,128},P{2,7,11},P{3,7,222},P{4,7,16},P{2,8,87},P{3,8,161},P{2,9,165},P{3,9,83},P{2,10,214},P{3,10,38},P{2,11,242},P{3,11,12}}},
  G{7,{P{1,3,47},P{2,3,201},P{3,3,244},P{4,3,201},P{5,3,48},P{1,4,206},P{2,4,97},P{3,4,10},P{4,4,100},P{5,4,205},P{1,5,246},P{2,5,7},P{4,5,6},P{5,5,246},P{1,6,184},P{2,6,103},P{3,6,11},P{4,6,96},P{5,6,181},P{1,7,38},P{2,7,235},P{3,7,255},P{4,7,235},P{5,7,39},P{1,8,188},P{2,8,102},P{3,8,11},P{4,8,99},P{5,8,192},P{1,9,247},P{2,9,8},P{4,9,8},P{5,9,246},P{1,10,209},P{2,10,100},P{3,10,9},P{4,10,98},P{5,10,206},P{1,11,51},P{2,11,206},P{3,11,246},P{4,11,206},P{5,11,51}}},
  G{7,{P{1,3,31},P{2,3,191},P{3,3,245},P{4,3,184},P{5,3,14},P{1,4,179},P{2,4,129},P{3,4,15},P{4,4,126},P{5,4,138},P{1,5,240},P{2,5,15},P{4,5,49},P{5,5,213},P{1,6,240},P{2,6,14},P{4,6,41},P{5,6,245},P{1,7,182},P{2,7,127},P{3,7,13},P{4,7,144},P{5,7,254},P{1,8,38},P{2,8,208},P{3,8,238},P{4,8,120},P{5,8,248},P{4,9,52},P{5,9,226},P{1,10,206},P{2,10,82},P{3,10,16},P{4,10,145},P{5,10,143},P{1,11,70},P{2,11,225},P{3,11,245},P{4,11,154},P{5,11,6}}},
  G{4,{P{2,5,255},P{2,11,255}}},
  G{4,{P{2,5,255},P{2,11,255},P{1,12,25},P{2,12,238},P{1,13,165},P{2,13,95}}},
  G{8,{P{5,4,73},P{6,4,195},P{2,5,2},P{3,5,85},P{4,5,205},P{5,5,183},P{6,5,66},P{1,6,97},P{2,6,210},P{3,6,159},P{4,6,43},P{1,7,233},P{2,7,50},P{1,8,101},P{2,8,212},P{3,8,158},P{4,8,43},P{2,9,3},P{3,9,88},P{4,9,207},P{5,9,182},P{6,9,66},P{5,10,74},P{6,10,195}}},
  G{8,{P{1,6,255},P{2,6,255},P{3,6,255},P{4,6,255},P{5,6,255},P{6,6,255},P{1,9,255},P{2,9,255},P{3,9,255},P{4,9,255},P{5,9,255},P{6,9,255}}},
  G{8,{P{1,4,194},P{2,4,72},P{1,5,66},P{2,5,183},P{3,5,204},P{4,5,84},P{5,5,2},P{3,6,43},P{4,6,158},P{5,6,209},P{6,6,96},P{5,7,49},P{6,7,232},P{3,8,42},P{4,8,157},P{5,8,212},P{6,8,101},P{1,9,66},P{2,9,182},P{3,9,206},P{4,9,87},P{5,9,2},P{1,10,195},P{2,10,74}}},
  G{7,{P{1,3,53},P{2,3,204},P{3,3,245},P{4,3,201},P{5,3,42},P{1,4,217},P{2,4,90},P{3,4,9},P{4,4,114},P{5,4,193},P{1,5,4},P{4,5,11},P{5,5,245},P{4,6,41},P{5,6,223},P{3,7,8},P{4,7,194},P{5,7,108},P{3,8,151},P{4,8,125},P{3,9,239},P{4,9,21},P{3,11,255}}},
  G{13,{P{3,3,9},P{4,3,105},P{5,3,198},P{6,3,236},P{7,3,248},P{8,3,217},P{9,3,139},P{10,3,20},P{2,4,31},P{3,4,202},P{4,4,106},P{5,4,8},P{7,4,7},P{8,4,39},P{9,4,110},P{10,4,214},P{11,4,27},P{1,5,7},P{2,5,194},P{3,5,64},P{10,5,123},P{11,5,161},P{1,6,106},P{2,6,116},P{4,6,100},P{5,6,218},P{6,6,249},P{7,6,203},P{8,6,85},P{9,6,182},P{10,6,26},P{11,6,235},P{1,7,198},P{2,7,53},P{3,7,82},P{4,7,198},P{5,7,51},P{6,7,8},P{7,7,42},P{8,7,217},P{9,7,136},P{10,7,9},P{11,7,245},P{1,8,236},P{2,8,17},P{3,8,223},P{4,8,31},P{7,8,6},P{8,8,220},P{9,8,55},P{10,8,52},P{11,8,181},P{1,9,249},P{2,9,8},P{3,9,234},P{4,9,68},P{5,9,7},P{6,9,51},P{7,9,152},P{8,9,254},P{9,9,28},P{10,9,174},P{11,9,43},P{1,10,213},P{2,10,34},P{3,10,82},P{4,10,222},P{5,10,245},P{6,10,186},P{7,10,35},P{8,10,148},P{9,10,243},P{10,10,93},P{1,11,123},P{2,11,131},P{1,12,7},P{2,12,192},P{3,12,135},P{4,12,38},P{5,12,11},P{6,12,7},P{7,12,34},P{8,12,94},P{9,12,167},P{10,12,3},P{2,13,6},P{3,13,117},P{4,13,208},P{5,13,246},P{6,13,247},P{7,13,216},P{8,13,149},P{9,13,49}}},
  G{9,{P{3,3,97},P{4,3,255},P{5,3,104},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{9,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,253},P{5,3,232},P{6,3,171},P{7,3,34},P{1,4,255},P{4,4,1},P{5,4,23},P{6,4,129},P{7,4,203},P{1,5,255},P{6,5,13},P{7,5,245},P{1,6,255},P{4,6,2},P{5,6,26},P{6,6,141},P{7,6,171},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,231},P{7,7,36},P{1,8,255},P{4,8,2},P{5,8,29},P{6,8,140},P{7,8,195},P{1,9,255},P{6,9,16},P{7,9,248},P{1,10,255},P{4,10,1},P{5,10,20},P{6,10,137},P{7,10,195},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,254},P{5,11,235},P{6,11,167},P{7,11,24}}},
  G{9,{P{2,3,72},P{3,3,205},P{4,3,248},P{5,3,234},P{6,3,160},P{7,3,21},P{1,4,61},P{2,4,238},P{3,4,84},P{4,4,7},P{5,4,29},P{6,4,146},P{7,4,156},P{1,5,179},P{2,5,109},P{1,6,225},P{2,6,29},P{1,7,247},P{2,7,7},P{1,8,222},P{2,8,34},P{1,9,165},P{2,9,120},P{1,10,46},P{2,10,241},P{3,10,92},P{4,10,8},P{5,10,21},P{6,10,119},P{7,10,172},P{2,11,71},P{3,11,206},P{4,11,248},P{5,11,233},P{6,11,160},P{7,11,22}}},
  G{9,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,236},P{5,3,174},P{6,3,46},P{1,4,255},P{3,4,2},P{4,4,33},P{5,4,128},P{6,4,239},P{7,4,44},P{1,5,255},P{6,5,126},P{7,5,171},P{1,6,255},P{6,6,25},P{7,6,235},P{1,7,255},P{6,7,8},P{7,7,250},P{1,8,255},P{6,8,42},P{7,8,228},P{1,9,255},P{6,9,139},P{7,9,159},P{1,10,255},P{4,10,27},P{5,10,123},P{6,10,237},P{7,10,36},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,244},P{5,11,182},P{6,11,45}}},
  G{9,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{7,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,255},P{7,11,255}}},
  G{8,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{10,{P{2,3,73},P{3,3,204},P{4,3,248},P{5,3,236},P{6,3,172},P{7,3,29},P{1,4,61},P{2,4,235},P{3,4,80},P{4,4,7},P{5,4,24},P{6,4,131},P{7,4,177},P{1,5,179},P{2,5,103},P{1,6,225},P{2,6,26},P{1,7,250},P{2,7,7},P{5,7,255},P{6,7,255},P{7,7,255},P{8,7,255},P{1,8,230},P{2,8,38},P{8,8,255},P{1,9,162},P{2,9,136},P{7,9,2},P{8,9,255},P{1,10,35},P{2,10,235},P{3,10,124},P{4,10,20},P{5,10,8},P{6,10,56},P{7,10,182},P{8,10,119},P{2,11,40},P{3,11,174},P{4,11,238},P{5,11,248},P{6,11,213},P{7,11,110}}},
  G{9,{P{1,3,255},P{7,3,255},P{1,4,255},P{7,4,255},P{1,5,255},P{7,5,255},P{1,6,255},P{7,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{7,7,255},P{1,8,255},P{7,8,255},P{1,9,255},P{7,9,255},P{1,10,255},P{7,10,255},P{1,11,255},P{7,11,255}}},
  G{3,{P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{6,{P{2,3,255},P{3,3,255},P{4,3,255},P{4,4,255},P{4,5,255},P{4,6,255},P{4,7,255},P{4,8,255},P{3,9,9},P{4,9,244},P{0,10,198},P{1,10,125},P{2,10,8},P{3,10,110},P{4,10,186},P{0,11,59},P{1,11,209},P{2,11,246},P{3,11,198},P{4,11,37}}},
  G{9,{P{1,3,255},P{6,3,32},P{7,3,213},P{8,3,107},P{1,4,255},P{5,4,48},P{6,4,215},P{7,4,70},P{1,5,255},P{4,5,66},P{5,5,207},P{6,5,41},P{1,6,255},P{3,6,87},P{4,6,190},P{5,6,20},P{1,7,255},P{2,7,111},P{3,7,216},P{4,7,215},P{5,7,21},P{1,8,255},P{2,8,113},P{3,8,1},P{4,8,127},P{5,8,208},P{6,8,17},P{1,9,255},P{5,9,111},P{6,9,201},P{7,9,14},P{1,10,255},P{6,10,97},P{7,10,192},P{8,10,11},P{1,11,255},P{7,11,83},P{8,11,181},P{9,11,8}}},
  G{7,{P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255}}},
  G{11,{P{1,3,255},P{2,3,71},P{8,3,59},P{9,3,255},P{1,4,250},P{2,4,179},P{3,4,1},P{8,4,171},P{9,4,248},P{1,5,255},P{2,5,146},P{3,5,84},P{7,5,73},P{8,5,148},P{9,5,255},P{1,6,255},P{2,6,40},P{3,6,213},P{4,6,3},P{6,6,1},P{7,6,204},P{8,6,42},P{9,6,255},P{1,7,255},P{3,7,179},P{4,7,97},P{6,7,86},P{7,7,181},P{9,7,255},P{1,8,255},P{3,8,64},P{4,8,224},P{5,8,9},P{6,8,216},P{7,8,65},P{9,8,255},P{1,9,255},P{4,9,204},P{5,9,149},P{6,9,204},P{9,9,255},P{1,10,255},P{4,10,89},P{5,10,254},P{6,10,89},P{9,10,255},P{1,11,255},P{4,11,4},P{5,11,195},P{6,11,4},P{9,11,255}}},
  G{9,{P{1,3,250},P{2,3,48},P{7,3,255},P{1,4,248},P{2,4,179},P{3,4,6},P{7,4,255},P{1,5,252},P{2,5,67},P{3,5,133},P{7,5,255},P{1,6,255},P{3,6,146},P{4,6,56},P{7,6,255},P{1,7,255},P{3,7,11},P{4,7,182},P{5,7,9},P{7,7,255},P{1,8,255},P{4,8,61},P{5,8,142},P{7,8,255},P{1,9,255},P{5,9,139},P{6,9,64},P{7,9,251},P{1,10,255},P{5,10,8},P{6,10,183},P{7,10,246},P{1,11,255},P{6,11,54},P{7,11,252}}},
  G{10,{P{2,3,56},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,48},P{2,4,239},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,172},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,252},P{2,7,8},P{7,7,8},P{8,7,251},P{1,8,232},P{2,8,35},P{7,8,32},P{8,8,229},P{1,9,167},P{2,9,127},P{7,9,121},P{8,9,162},P{1,10,43},P{2,10,239},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{2,11,53},P{3,11,189},P{4,11,244},P{5,11,244},P{6,11,185},P{7,11,48}}},
  G{9,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,232},P{6,3,168},P{7,3,28},P{1,4,255},P{4,4,1},P{5,4,22},P{6,4,135},P{7,4,191},P{1,5,255},P{6,5,14},P{7,5,242},P{1,6,255},P{5,6,21},P{6,6,135},P{7,6,182},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,231},P{6,7,161},P{7,7,22},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{10,{P{2,3,55},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,47},P{2,4,238},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,171},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,249},P{2,7,8},P{7,7,8},P{8,7,249},P{1,8,224},P{2,8,35},P{7,8,32},P{8,8,221},P{1,9,164},P{2,9,127},P{7,9,121},P{8,9,156},P{1,10,42},P{2,10,237},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,229},P{8,10,35},P{2,11,47},P{3,11,194},P{4,11,248},P{5,11,249},P{6,11,182},P{7,11,43},P{4,12,38},P{5,12,243},P{6,12,58},P{7,12,6},P{5,13,106},P{6,13,235},P{7,13,240}}},
  G{9,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,238},P{6,3,178},P{7,3,33},P{1,4,255},P{5,4,18},P{6,4,128},P{7,4,195},P{1,5,255},P{6,5,13},P{7,5,244},P{1,6,255},P{5,6,16},P{6,6,135},P{7,6,187},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,253},P{5,7,206},P{6,7,160},P{7,7,23},P{1,8,255},P{4,8,146},P{5,8,91},P{1,9,255},P{4,9,7},P{5,9,196},P{6,9,40},P{1,10,255},P{5,10,40},P{6,10,200},P{7,10,9},P{1,11,255},P{6,11,97},P{7,11,159}}},
  G{9,{P{1,3,27},P{2,3,164},P{3,3,230},P{4,3,251},P{5,3,240},P{6,3,183},P{7,3,49},P{1,4,203},P{2,4,125},P{3,4,26},P{4,4,4},P{5,4,18},P{6,4,94},P{7,4,148},P{1,5,245},P{2,5,10},P{1,6,155},P{2,6,86},P{3,6,89},P{4,6,42},P{5,6,1},P{1,7,2},P{2,7,90},P{3,7,174},P{4,7,233},P{5,7,235},P{6,7,150},P{7,7,19},P{5,8,35},P{6,8,127},P{7,8,189},P{6,9,17},P{7,9,243},P{1,10,188},P{2,10,122},P{3,10,23},P{4,10,4},P{5,10,24},P{6,10,141},P{7,10,182},P{1,11,31},P{2,11,169},P{3,11,233},P{4,11,251},P{5,11,235},P{6,11,156},P{7,11,22}}},
  G{7,{P{0,3,255},P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{3,4,255},P{3,5,255},P{3,6,255},P{3,7,255},P{3,8,255},P{3,9,255},P{3,10,255},P{3,11,255}}},
  G{9,{P{1,3,255},P{7,3,255},P{1,4,255},P{7,4,255},P{1,5,255},P{7,5,255},P{1,6,255},P{7,6,255},P{1,7,255},P{7,7,255},P{1,8,254},P{2,8,4},P{6,8,6},P{7,8,253},P{1,9,227},P{2,9,44},P{6,9,55},P{7,9,219},P{1,10,91},P{2,10,196},P{3,10,42},P{4,10,8},P{5,10,55},P{6,10,210},P{7,10,84},P{2,11,70},P{3,11,208},P{4,11,247},P{5,11,215},P{6,11,102}}},
  G{9,{P{0,3,201},P{1,3,114},P{7,3,114},P{8,3,200},P{0,4,92},P{1,4,206},P{7,4,207},P{8,4,90},P{0,5,6},P{1,5,231},P{2,5,41},P{6,5,42},P{7,5,230},P{8,5,5},P{1,6,128},P{2,6,134},P{6,6,135},P{7,6,126},P{1,7,24},P{2,7,218},P{3,7,1},P{5,7,1},P{6,7,218},P{7,7,22},P{2,8,165},P{3,8,62},P{5,8,63},P{6,8,161},P{2,9,55},P{3,9,190},P{4,9,11},P{5,9,190},P{6,9,51},P{3,10,201},P{4,10,175},P{5,10,197},P{3,11,91},P{4,11,255},P{5,11,87}}},
  G{13,{P{0,3,210},P{1,3,108},P{5,3,17},P{6,3,247},P{7,3,34},P{11,3,104},P{12,3,209},P{0,4,118},P{1,4,188},P{5,4,108},P{6,4,254},P{7,4,126},P{11,4,184},P{12,4,117},P{0,5,28},P{1,5,249},P{2,5,17},P{5,5,206},P{6,5,165},P{7,5,218},P{10,5,14},P{11,5,246},P{12,5,27},P{1,6,190},P{2,6,93},P{4,6,48},P{5,6,216},P{6,6,18},P{7,6,214},P{8,6,56},P{10,6,88},P{11,6,188},P{1,7,98},P{2,7,174},P{4,7,147},P{5,7,103},P{7,7,105},P{8,7,149},P{10,7,168},P{11,7,95},P{1,8,14},P{2,8,236},P{3,8,12},P{4,8,227},P{5,8,15},P{7,8,16},P{8,8,227},P{9,8,8},P{10,8,231},P{11,8,13},P{2,9,170},P{3,9,129},P{4,9,164},P{8,9,167},P{9,9,126},P{10,9,166},P{2,10,78},P{3,10,243},P{4,10,66},P{8,10,70},P{9,10,243},P{10,10,74},P{2,11,5},P{3,11,203},P{4,11,1},P{8,11,2},P{9,11,204},P{10,11,4}}},
  G{7,{P{0,3,170},P{1,3,101},P{5,3,99},P{6,3,163},P{0,4,21},P{1,4,214},P{2,4,19},P{4,4,16},P{5,4,199},P{6,4,13},P{1,5,83},P{2,5,154},P{4,5,140},P{5,5,55},P{2,6,165},P{3,6,99},P{4,6,118},P{2,7,69},P{3,7,253},P{4,7,35},P{1,8,3},P{2,8,206},P{3,8,120},P{4,8,194},P{5,8,3},P{1,9,114},P{2,9,162},P{4,9,153},P{5,9,124},P{0,10,29},P{1,10,225},P{2,10,21},P{4,10,18},P{5,10,230},P{6,10,53},P{0,11,176},P{1,11,99},P{5,11,98},P{6,11,218},P{7,11,11}}},
  G{9,{P{1,3,175},P{2,3,68},P{6,3,68},P{7,3,174},P{1,4,28},P{2,4,202},P{3,4,5},P{5,4,5},P{6,4,200},P{7,4,27},P{2,5,109},P{3,5,119},P{5,5,115},P{6,5,108},P{2,6,2},P{3,6,190},P{4,6,52},P{5,6,187},P{6,6,2},P{3,7,47},P{4,7,246},P{5,7,47},P{4,8,255},P{4,9,255},P{4,10,255},P{4,11,255}}},
  G{7,{P{1,3,136},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{4,4,102},P{5,4,170},P{3,5,11},P{4,5,226},P{5,5,36},P{3,6,127},P{4,6,148},P{2,7,23},P{3,7,230},P{4,7,22},P{2,8,151},P{3,8,126},P{1,9,39},P{2,9,227},P{3,9,11},P{1,10,175},P{2,10,105},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,76}}},
  G{4,{P{1,3,255},P{2,3,255},P{3,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{1,12,255},P{1,13,255},P{1,14,255},P{2,14,255},P{3,14,255}}},
  G{4,{P{-1,3,21},P{0,3,218},P{1,3,2},P{0,4,174},P{1,4,71},P{0,5,77},P{1,5,173},P{0,6,4},P{1,6,226},P{2,6,23},P{1,7,138},P{2,7,119},P{1,8,41},P{2,8,219},P{3,8,1},P{2,9,199},P{3,9,66},P{2,10,102},P{3,10,167},P{2,11,14},P{3,11,239},P{4,11,20}}},
  G{4,{P{0,3,255},P{1,3,255},P{2,3,255},P{2,4,255},P{2,5,255},P{2,6,255},P{2,7,255},P{2,8,255},P{2,9,255},P{2,10,255},P{2,11,255},P{2,12,255},P{2,13,255},P{0,14,255},P{1,14,255},P{2,14,255}}},
  G{5,{P{1,3,80},P{2,3,243},P{3,3,87},P{1,4,175},P{2,4,91},P{3,4,180},P{0,5,21},P{1,5,199},P{3,5,201},P{4,5,23},P{0,6,112},P{1,6,108},P{3,6,109},P{4,6,114},P{0,7,201},P{1,7,18},P{3,7,19},P{4,7,201}}},
  G{7,{P{0,14,255},P{1,14,255},P{2,14,255},P{3,14,255},P{4,14,255},P{5,14,255},P{6,14,255}}},
  G{4,{P{1,2,183},P{2,2,62},P{1,3,13},P{2,3,155}}},
  G{7,{P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{7,{P{1,3,255},P{1,4,255},P{1,5,255},P{2,5,82},P{3,5,239},P{4,5,214},P{5,5,44},P{1,6,254},P{2,6,132},P{3,6,14},P{4,6,125},P{5,6,180},P{1,7,255},P{2,7,26},P{4,7,22},P{5,7,234},P{1,8,255},P{2,8,5},P{4,8,5},P{5,8,249},P{1,9,255},P{2,9,23},P{4,9,24},P{5,9,232},P{1,10,253},P{2,10,125},P{3,10,12},P{4,10,124},P{5,10,175},P{0,11,4},P{1,11,243},P{2,11,81},P{3,11,239},P{4,11,211},P{5,11,40}}},
  G{7,{P{1,5,20},P{2,5,181},P{3,5,246},P{4,5,208},P{5,5,53},P{1,6,160},P{2,6,135},P{3,6,12},P{4,6,81},P{5,6,205},P{1,7,233},P{2,7,22},P{1,8,250},P{2,8,4},P{1,9,230},P{2,9,28},P{1,10,156},P{2,10,146},P{3,10,16},P{4,10,86},P{5,10,212},P{1,11,19},P{2,11,184},P{3,11,248},P{4,11,211},P{5,11,56}}},
  G{7,{P{5,3,255},P{5,4,255},P{1,5,40},P{2,5,210},P{3,5,235},P{4,5,76},P{5,5,255},P{1,6,176},P{2,6,127},P{3,6,13},P{4,6,126},P{5,6,255},P{1,7,234},P{2,7,21},P{4,7,26},P{5,7,255},P{1,8,250},P{2,8,4},P{4,8,6},P{5,8,255},P{1,9,235},P{2,9,21},P{4,9,29},P{5,9,255},P{1,10,182},P{2,10,122},P{3,10,12},P{4,10,128},P{5,10,254},P{1,11,46},P{2,11,214},P{3,11,236},P{4,11,78},P{5,11,246},P{6,11,1}}},
  G{7,{P{1,5,25},P{2,5,186},P{3,5,245},P{4,5,191},P{5,5,16},P{1,6,169},P{2,6,141},P{3,6,12},P{4,6,136},P{5,6,149},P{1,7,236},P{2,7,24},P{4,7,22},P{5,7,223},P{1,8,250},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,248},P{1,9,227},P{2,9,13},P{1,10,155},P{2,10,129},P{3,10,10},P{4,10,99},P{5,10,189},P{1,11,21},P{2,11,179},P{3,11,243},P{4,11,213},P{5,11,61}}},
  G{3,{P{0,3,4},P{1,3,135},P{2,3,246},P{0,4,33},P{1,4,255},P{2,4,17},P{0,5,248},P{1,5,255},P{2,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{7,{P{1,5,38},P{2,5,216},P{3,5,239},P{4,5,94},P{5,5,246},P{6,5,1},P{1,6,178},P{2,6,129},P{3,6,15},P{4,6,150},P{5,6,254},P{1,7,236},P{2,7,23},P{4,7,38},P{5,7,255},P{1,8,251},P{2,8,4},P{4,8,8},P{5,8,255},P{1,9,238},P{2,9,19},P{4,9,36},P{5,9,255},P{1,10,187},P{2,10,120},P{3,10,14},P{4,10,147},P{5,10,255},P{1,11,52},P{2,11,220},P{3,11,234},P{4,11,86},P{5,11,255},P{4,12,22},P{5,12,239},P{1,13,174},P{2,13,78},P{3,13,13},P{4,13,140},P{5,13,184},P{1,14,74},P{2,14,220},P{3,14,246},P{4,14,196},P{5,14,38}}},
  G{7,{P{1,3,255},P{1,4,255},P{1,5,253},P{2,5,92},P{3,5,236},P{4,5,231},P{5,5,89},P{1,6,251},P{2,6,138},P{3,6,14},P{4,6,84},P{5,6,224},P{1,7,255},P{2,7,19},P{4,7,4},P{5,7,253},P{1,8,255},P{5,8,255},P{1,9,255},P{5,9,255},P{1,10,255},P{5,10,255},P{1,11,255},P{5,11,255}}},
  G{3,{P{1,3,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{1,3,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{1,12,255},P{0,13,31},P{1,13,246},P{0,14,249},P{1,14,121}}},
  G{7,{P{1,3,255},P{1,4,255},P{1,5,255},P{4,5,6},P{5,5,164},P{6,5,121},P{1,6,255},P{3,6,17},P{4,6,191},P{5,6,109},P{1,7,255},P{2,7,33},P{3,7,212},P{4,7,97},P{1,8,255},P{2,8,229},P{3,8,241},P{4,8,67},P{1,9,255},P{2,9,51},P{3,9,68},P{4,9,231},P{5,9,41},P{1,10,255},P{4,10,75},P{5,10,217},P{6,10,22},P{1,11,255},P{5,11,83},P{6,11,196},P{7,11,9}}},
  G{3,{P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{11,{P{0,5,7},P{1,5,239},P{2,5,90},P{3,5,232},P{4,5,236},P{5,5,91},P{6,5,99},P{7,5,233},P{8,5,235},P{9,5,90},P{0,6,1},P{1,6,251},P{2,6,137},P{3,6,13},P{4,6,85},P{5,6,237},P{6,6,141},P{7,6,13},P{8,6,85},P{9,6,223},P{1,7,255},P{2,7,20},P{4,7,6},P{5,7,255},P{6,7,19},P{8,7,6},P{9,7,253},P{1,8,255},P{5,8,255},P{9,8,255},P{1,9,255},P{5,9,255},P{9,9,255},P{1,10,255},P{5,10,255},P{9,10,255},P{1,11,255},P{5,11,255},P{9,11,255}}},
  G{7,{P{0,5,7},P{1,5,236},P{2,5,93},P{3,5,233},P{4,5,234},P{5,5,90},P{0,6,1},P{1,6,250},P{2,6,139},P{3,6,13},P{4,6,85},P{5,6,223},P{1,7,255},P{2,7,20},P{4,7,6},P{5,7,253},P{1,8,255},P{5,8,255},P{1,9,255},P{5,9,255},P{1,10,255},P{5,10,255},P{1,11,255},P{5,11,255}}},
  G{7,{P{1,5,27},P{2,5,191},P{3,5,243},P{4,5,194},P{5,5,31},P{1,6,164},P{2,6,136},P{3,6,13},P{4,6,131},P{5,6,167},P{1,7,230},P{2,7,23},P{4,7,22},P{5,7,229},P{1,8,249},P{2,8,3},P{4,8,3},P{5,8,249},P{1,9,228},P{2,9,22},P{4,9,22},P{5,9,228},P{1,10,160},P{2,10,132},P{3,10,11},P{4,10,137},P{5,10,161},P{1,11,26},P{2,11,192},P{3,11,243},P{4,11,188},P{5,11,26}}},
  G{7,{P{0,5,4},P{1,5,243},P{2,5,86},P{3,5,241},P{4,5,221},P{5,5,44},P{1,6,253},P{2,6,132},P{3,6,13},P{4,6,127},P{5,6,181},P{1,7,255},P{2,7,24},P{4,7,21},P{5,7,236},P{1,8,255},P{2,8,3},P{4,8,4},P{5,8,250},P{1,9,255},P{2,9,23},P{4,9,21},P{5,9,235},P{1,10,254},P{2,10,126},P{3,10,13},P{4,10,127},P{5,10,177},P{1,11,254},P{2,11,83},P{3,11,239},P{4,11,219},P{5,11,39},P{1,12,255},P{1,13,255},P{1,14,255}}},
  G{7,{P{1,5,41},P{2,5,211},P{3,5,239},P{4,5,83},P{5,5,247},P{6,5,5},P{1,6,177},P{2,6,128},P{3,6,13},P{4,6,132},P{5,6,255},P{1,7,234},P{2,7,22},P{4,7,26},P{5,7,255},P{1,8,250},P{2,8,3},P{4,8,4},P{5,8,255},P{1,9,235},P{2,9,18},P{4,9,26},P{5,9,255},P{1,10,182},P{2,10,121},P{3,10,12},P{4,10,135},P{5,10,255},P{1,11,46},P{2,11,214},P{3,11,240},P{4,11,86},P{5,11,255},P{5,12,255},P{5,13,255},P{5,14,255}}},
  G{4,{P{0,5,8},P{1,5,237},P{2,5,125},P{3,5,246},P{0,6,1},P{1,6,249},P{2,6,127},P{3,6,9},P{1,7,255},P{2,7,24},P{1,8,255},P{2,8,1},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{7,{P{1,5,84},P{2,5,219},P{3,5,248},P{4,5,212},P{5,5,63},P{1,6,235},P{2,6,53},P{3,6,5},P{4,6,89},P{5,6,205},P{1,7,212},P{2,7,135},P{3,7,31},P{1,8,33},P{2,8,155},P{3,8,233},P{4,8,207},P{5,8,62},P{3,9,1},P{4,9,92},P{5,9,226},P{1,10,200},P{2,10,86},P{3,10,5},P{4,10,58},P{5,10,229},P{1,11,70},P{2,11,216},P{3,11,249},P{4,11,217},P{5,11,75}}},
  G{4,{P{1,3,187},P{1,4,225},P{0,5,200},P{1,5,255},P{2,5,255},P{3,5,132},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,228},P{2,10,36},P{3,10,6},P{1,11,47},P{2,11,219},P{3,11,234}}},
  G{7,{P{1,5,255},P{5,5,255},P{1,6,255},P{5,6,255},P{1,7,255},P{5,7,255},P{1,8,255},P{5,8,255},P{1,9,253},P{2,9,5},P{4,9,23},P{5,9,255},P{1,10,228},P{2,10,82},P{3,10,13},P{4,10,140},P{5,10,251},P{1,11,95},P{2,11,235},P{3,11,235},P{4,11,92},P{5,11,238},P{6,11,1}}},
  G{5,{P{-1,5,27},P{0,5,254},P{1,5,18},P{3,5,15},P{4,5,253},P{5,5,27},P{0,6,206},P{1,6,70},P{3,6,67},P{4,6,206},P{0,7,130},P{1,7,122},P{3,7,119},P{4,7,130},P{0,8,54},P{1,8,175},P{3,8,172},P{4,8,54},P{0,9,1},P{1,9,208},P{2,9,26},P{3,9,206},P{4,9,1},P{1,10,158},P{2,10,185},P{3,10,158},P{1,11,82},P{2,11,255},P{3,11,82}}},
  G{9,{P{-1,5,23},P{0,5,246},P{1,5,13},P{3,5,12},P{4,5,240},P{5,5,12},P{7,5,13},P{8,5,246},P{9,5,23},P{0,6,191},P{1,6,85},P{3,6,87},P{4,6,251},P{5,6,86},P{7,6,86},P{8,6,190},P{0,7,105},P{1,7,164},P{3,7,168},P{4,7,166},P{5,7,167},P{7,7,165},P{8,7,104},P{0,8,22},P{1,8,235},P{2,8,12},P{3,8,237},P{4,8,36},P{5,8,237},P{6,8,11},P{7,8,235},P{8,8,21},P{1,9,188},P{2,9,135},P{3,9,185},P{5,9,189},P{6,9,135},P{7,9,187},P{1,10,102},P{2,10,245},P{3,10,103},P{5,10,107},P{6,10,245},P{7,10,101},P{1,11,20},P{2,11,249},P{3,11,23},P{5,11,26},P{6,11,249},P{7,11,19}}},
  G{7,{P{0,5,12},P{1,5,226},P{2,5,73},P{4,5,77},P{5,5,226},P{6,5,12},P{1,6,77},P{2,6,210},P{3,6,5},P{4,6,212},P{5,6,75},P{2,7,171},P{3,7,184},P{4,7,168},P{2,8,64},P{3,8,255},P{4,8,64},P{2,9,191},P{3,9,154},P{4,9,192},P{1,10,90},P{2,10,197},P{3,10,1},P{4,10,203},P{5,10,90},P{0,11,13},P{1,11,230},P{2,11,63},P{4,11,73},P{5,11,230},P{6,11,13}}},
  G{7,{P{-1,5,17},P{0,5,221},P{1,5,8},P{5,5,10},P{6,5,220},P{7,5,17},P{0,6,144},P{1,6,102},P{5,6,117},P{6,6,142},P{0,7,31},P{1,7,212},P{2,7,1},P{4,7,11},P{5,7,229},P{6,7,29},P{1,8,166},P{2,8,80},P{4,8,118},P{5,8,161},P{1,9,49},P{2,9,233},P{3,9,20},P{4,9,231},P{5,9,44},P{2,10,188},P{3,10,196},P{4,10,181},P{2,11,71},P{3,11,255},P{4,11,63},P{2,12,86},P{3,12,200},P{0,13,4},P{1,13,61},P{2,13,232},P{3,13,65},P{0,14,249},P{1,14,227},P{2,14,96}}},
  G{7,{P{1,5,192},P{2,5,255},P{3,5,255},P{4,5,255},P{5,5,255},P{4,6,113},P{5,6,151},P{3,7,64},P{4,7,194},P{5,7,6},P{2,8,28},P{3,8,209},P{4,8,26},P{1,9,7},P{2,9,196},P{3,9,60},P{1,10,155},P{2,10,109},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,60}}},
  G{4,{P{1,3,66},P{2,3,236},P{3,3,255},P{1,4,223},P{2,4,73},P{3,4,2},P{1,5,255},P{0,6,2},P{1,6,255},P{-1,7,2},P{0,7,84},P{1,7,228},P{-1,8,92},P{0,8,250},P{1,8,73},P{-1,9,3},P{0,9,98},P{1,9,213},P{0,10,5},P{1,10,255},P{1,11,255},P{1,12,255},P{2,12,3},P{1,13,236},P{2,13,87},P{3,13,3},P{1,14,97},P{2,14,234},P{3,14,255}}},
  G{3,{P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{1,12,255},P{1,13,255},P{1,14,255}}},
  G{4,{P{0,3,255},P{1,3,236},P{2,3,66},P{0,4,3},P{1,4,76},P{2,4,222},P{2,5,255},P{2,6,255},P{3,6,1},P{2,7,229},P{3,7,84},P{4,7,2},P{2,8,76},P{3,8,249},P{4,8,96},P{2,9,217},P{3,9,98},P{4,9,3},P{2,10,255},P{3,10,5},P{2,11,255},P{1,12,3},P{2,12,255},P{0,13,3},P{1,13,90},P{2,13,236},P{0,14,255},P{1,14,234},P{2,14,97}}},
  G{8,{P{1,6,107},P{2,6,208},P{3,6,200},P{4,6,86},P{5,6,10},P{6,6,123},P{1,7,170},P{2,7,30},P{3,7,74},P{4,7,189},P{5,7,246},P{6,7,107}}},
  G{5,{P{0,3,255},P{1,3,255},P{2,3,255},P{3,3,255},P{0,4,255},P{3,4,255},P{0,5,255},P{3,5,255},P{0,6,255},P{3,6,255},P{0,7,255},P{3,7,255},P{0,8,255},P{3,8,255},P{0,9,255},P{3,9,255},P{0,10,255},P{3,10,255},P{0,11,255},P{1,11,255},P{2,11,255},P{3,11,255}}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{0,{}},
  G{4,{}},
  G{3,{P{1,5,255},P{1,7,196},P{1,8,206},P{1,9,215},P{1,10,224},P{1,11,233},P{1,12,242},P{1,13,251}}},
  G{7,{P{3,3,255},P{1,4,36},P{2,4,210},P{3,4,255},P{4,4,206},P{5,4,41},P{1,5,170},P{2,5,103},P{3,5,255},P{4,5,102},P{5,5,195},P{1,6,231},P{2,6,16},P{3,6,255},P{4,6,3},P{5,6,27},P{1,7,250},P{2,7,3},P{3,7,255},P{1,8,234},P{2,8,20},P{3,8,255},P{4,8,3},P{5,8,52},P{1,9,172},P{2,9,111},P{3,9,255},P{4,9,101},P{5,9,199},P{1,10,32},P{2,10,207},P{3,10,255},P{4,10,213},P{5,10,44},P{3,11,255}}},
  G{7,{P{2,3,57},P{3,3,205},P{4,3,246},P{5,3,216},P{6,3,75},P{2,4,214},P{3,4,92},P{4,4,8},P{5,4,81},P{6,4,150},P{2,5,253},P{3,5,2},P{2,6,255},P{0,7,16},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,104},P{2,8,255},P{1,9,18},P{2,9,232},P{6,9,10},P{1,10,130},P{2,10,119},P{5,10,78},P{6,10,224},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,235},P{6,11,83}}},
  G{7,{P{1,4,4},P{6,4,3},P{0,5,5},P{1,5,209},P{2,5,128},P{3,5,219},P{4,5,218},P{5,5,131},P{6,5,202},P{7,5,2},P{1,6,135},P{2,6,183},P{3,6,30},P{4,6,31},P{5,6,185},P{6,6,130},P{1,7,220},P{2,7,29},P{5,7,29},P{6,7,218},P{1,8,221},P{2,8,28},P{5,8,28},P{6,8,219},P{1,9,134},P{2,9,180},P{3,9,28},P{4,9,29},P{5,9,182},P{6,9,128},P{0,10,6},P{1,10,216},P{2,10,132},P{3,10,221},P{4,10,220},P{5,10,136},P{6,10,209},P{7,10,3},P{1,11,7},P{6,11,6}}},
  G{7,{P{0,3,170},P{1,3,64},P{5,3,65},P{6,3,170},P{0,4,23},P{1,4,209},P{2,4,17},P{4,4,18},P{5,4,210},P{6,4,24},P{1,5,94},P{2,5,172},P{4,5,173},P{5,5,97},P{2,6,183},P{3,6,177},P{4,6,186},P{0,7,108},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,112},P{3,8,254},P{0,9,108},P{1,9,255},P{2,9,255},P{3,9,255},P{4,9,255},P{5,9,255},P{6,9,112},P{3,10,255},P{3,11,255}}},
  G{3,{P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,10,255},P{1,11,255},P{1,12,255},P{1,13,255},P{1,14,255}}},
  G{7,{P{1,3,91},P{2,3,222},P{3,3,248},P{4,3,206},P{5,3,50},P{1,4,239},P{2,4,51},P{3,4,4},P{4,4,88},P{5,4,133},P{1,5,188},P{2,5,125},P{3,5,41},P{1,6,65},P{2,6,215},P{3,6,185},P{4,6,212},P{5,6,69},P{1,7,229},P{2,7,26},P{4,7,56},P{5,7,232},P{1,8,211},P{2,8,105},P{3,8,3},P{4,8,61},P{5,8,211},P{1,9,32},P{2,9,157},P{3,9,224},P{4,9,246},P{5,9,60},P{3,10,3},P{4,10,109},P{5,10,215},P{1,11,183},P{2,11,72},P{3,11,4},P{4,11,61},P{5,11,231},P{1,12,72},P{2,12,219},P{3,12,250},P{4,12,219},P{5,12,76}}},
  G{4,{P{0,3,255},P{2,3,255}}},
  G{10,{P{1,3,6},P{2,3,122},P{3,3,215},P{4,3,248},P{5,3,220},P{6,3,128},P{7,3,9},P{0,4,7},P{1,4,190},P{2,4,119},P{3,4,29},P{4,4,8},P{5,4,41},P{6,4,126},P{7,4,196},P{8,4,9},P{0,5,126},P{1,5,117},P{2,5,48},P{3,5,201},P{4,5,245},P{5,5,213},P{6,5,66},P{7,5,126},P{8,5,128},P{0,6,219},P{1,6,27},P{2,6,204},P{3,6,111},P{4,6,10},P{5,6,37},P{6,6,118},P{7,6,41},P{8,6,220},P{0,7,248},P{1,7,6},P{2,7,246},P{3,7,10},P{7,7,9},P{8,7,248},P{0,8,219},P{1,8,26},P{2,8,201},P{3,8,120},P{4,8,10},P{5,8,37},P{6,8,122},P{7,8,40},P{8,8,216},P{0,9,128},P{1,9,115},P{2,9,42},P{3,9,195},P{4,9,245},P{5,9,218},P{6,9,70},P{7,9,125},P{8,9,124},P{0,10,8},P{1,10,193},P{2,10,115},P{3,10,26},P{4,10,7},P{5,10,39},P{6,10,123},P{7,10,193},P{8,10,7},P{1,11,7},P{2,11,126},P{3,11,219},P{4,11,248},P{5,11,219},P{6,11,128},P{7,11,8}}},
  G{4,{P{0,3,144},P{1,3,244},P{2,3,93},P{0,4,80},P{1,4,27},P{2,4,245},P{0,5,119},P{1,5,249},P{2,5,255},P{0,6,239},P{1,6,58},P{2,6,255},P{3,6,26},P{0,7,125},P{1,7,192},P{2,7,112},P{3,7,246}}},
  G{7,{P{1,6,110},P{2,6,183},P{4,6,110},P{5,6,183},P{0,7,76},P{1,7,206},P{2,7,13},P{3,7,76},P{4,7,206},P{5,7,13},P{0,8,235},P{1,8,55},P{3,8,235},P{4,8,55},P{0,9,83},P{1,9,203},P{2,9,12},P{3,9,83},P{4,9,203},P{5,9,12},P{1,10,112},P{2,10,182},P{4,10,112},P{5,10,182}}},
  G{8,{P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{6,8,255},P{6,9,255},P{6,10,255}}},
  G{4,{P{1,8,255},P{2,8,255}}},
  G{10,{P{1,3,6},P{2,3,122},P{3,3,215},P{4,3,248},P{5,3,220},P{6,3,128},P{7,3,9},P{0,4,7},P{1,4,190},P{2,4,119},P{3,4,29},P{4,4,8},P{5,4,41},P{6,4,126},P{7,4,196},P{8,4,9},P{0,5,126},P{1,5,117},P{3,5,255},P{4,5,255},P{5,5,239},P{6,5,77},P{7,5,126},P{8,5,128},P{0,6,219},P{1,6,27},P{3,6,255},P{5,6,59},P{6,6,230},P{7,6,41},P{8,6,220},P{0,7,248},P{1,7,6},P{3,7,255},P{4,7,255},P{5,7,248},P{6,7,76},P{7,7,9},P{8,7,248},P{0,8,219},P{1,8,26},P{3,8,255},P{5,8,159},P{6,8,61},P{7,8,40},P{8,8,216},P{0,9,128},P{1,9,115},P{3,9,255},P{5,9,19},P{6,9,199},P{7,9,137},P{8,9,124},P{0,10,8},P{1,10,193},P{2,10,115},P{3,10,26},P{4,10,7},P{5,10,39},P{6,10,123},P{7,10,193},P{8,10,7},P{1,11,7},P{2,11,126},P{3,11,219},P{4,11,248},P{5,11,219},P{6,11,128},P{7,11,8}}},
  G{7,{P{0,2,255},P{1,2,255},P{2,2,255},P{3,2,255},P{4,2,255},P{5,2,255},P{6,2,255}}},
  G{5,{P{1,3,73},P{2,3,227},P{3,3,226},P{4,3,73},P{1,4,227},P{2,4,59},P{3,4,60},P{4,4,227},P{1,5,227},P{2,5,59},P{3,5,60},P{4,5,227},P{1,6,73},P{2,6,227},P{3,6,226},P{4,6,73}}},
  G{7,{P{3,4,128},P{3,5,255},P{3,6,255},P{0,7,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{3,8,255},P{3,9,255},P{3,10,128},P{0,11,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,255}}},
  G{4,{P{0,3,97},P{1,3,230},P{2,3,243},P{3,3,101},P{0,4,94},P{1,4,34},P{2,4,43},P{3,4,229},P{1,5,64},P{2,5,182},P{3,5,55},P{0,6,96},P{1,6,137},P{2,6,7},P{0,7,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,20}}},
  G{4,{P{0,3,118},P{1,3,237},P{2,3,226},P{3,3,106},P{0,4,90},P{1,4,31},P{2,4,68},P{3,4,231},P{1,5,255},P{2,5,242},P{3,5,87},P{0,6,86},P{1,6,28},P{2,6,64},P{3,6,237},P{0,7,127},P{1,7,238},P{2,7,200},P{3,7,85}}},
  G{4,{P{1,2,13},P{2,2,208},P{3,2,105},P{1,3,164},P{2,3,74}}},
  G{7,{P{1,5,255},P{5,5,255},P{1,6,255},P{5,6,255},P{1,7,255},P{5,7,255},P{1,8,255},P{5,8,255},P{1,9,255},P{2,9,7},P{4,9,20},P{5,9,255},P{1,10,255},P{2,10,96},P{3,10,12},P{4,10,136},P{5,10,245},P{6,10,23},P{1,11,255},P{2,11,204},P{3,11,241},P{4,11,102},P{5,11,117},P{6,11,245},P{1,12,255},P{1,13,255},P{1,14,255}}},
  G{7,{P{1,3,54},P{2,3,212},P{3,3,253},P{4,3,255},P{5,3,255},P{6,3,212},P{1,4,205},P{2,4,255},P{3,4,255},P{5,4,255},P{1,5,247},P{2,5,255},P{3,5,255},P{5,5,255},P{1,6,215},P{2,6,255},P{3,6,255},P{5,6,255},P{1,7,70},P{2,7,228},P{3,7,255},P{5,7,255},P{3,8,255},P{5,8,255},P{3,9,255},P{5,9,255},P{3,10,255},P{5,10,255},P{3,11,255},P{5,11,255},P{3,12,255},P{5,12,255},P{3,13,255},P{5,13,255}}},
  G{4,{P{1,8,255}}},
  G{4,{P{1,12,203},P{2,12,108},P{1,13,38},P{2,13,228},P{1,14,249},P{2,14,115}}},
  G{4,{P{1,3,90},P{2,3,255},P{1,4,96},P{2,4,255},P{2,5,255},P{2,6,255},P{1,7,255},P{2,7,255},P{3,7,255}}},
  G{5,{P{0,3,74},P{1,3,229},P{2,3,231},P{3,3,80},P{0,4,216},P{1,4,62},P{2,4,61},P{3,4,219},P{0,5,248},P{1,5,5},P{2,5,6},P{3,5,248},P{0,6,218},P{1,6,61},P{2,6,65},P{3,6,215},P{0,7,78},P{1,7,231},P{2,7,230},P{3,7,75}}},
  G{7,{P{1,6,183},P{2,6,109},P{4,6,183},P{5,6,106},P{1,7,13},P{2,7,207},P{3,7,75},P{4,7,13},P{5,7,206},P{6,7,75},P{2,8,55},P{3,8,235},P{5,8,55},P{6,8,235},P{1,9,12},P{2,9,204},P{3,9,83},P{4,9,12},P{5,9,204},P{6,9,82},P{1,10,182},P{2,10,115},P{4,10,182},P{5,10,112}}},
  G{11,{P{1,3,90},P{2,3,255},P{7,3,17},P{8,3,205},P{9,3,13},P{1,4,96},P{2,4,255},P{7,4,154},P{8,4,82},P{2,5,255},P{6,5,58},P{7,5,178},P{2,6,255},P{5,6,4},P{6,6,200},P{7,6,32},P{1,7,255},P{2,7,255},P{3,7,255},P{5,7,118},P{6,7,119},P{7,7,110},P{8,7,255},P{4,8,31},P{5,8,201},P{6,8,33},P{7,8,157},P{8,8,255},P{4,9,177},P{5,9,60},P{6,9,165},P{7,9,31},P{8,9,255},P{3,10,82},P{4,10,157},P{6,10,255},P{7,10,255},P{8,10,255},P{9,10,180},P{2,11,12},P{3,11,207},P{4,11,19},P{8,11,255}}},
  G{11,{P{1,3,90},P{2,3,255},P{7,3,17},P{8,3,205},P{9,3,13},P{1,4,96},P{2,4,255},P{7,4,154},P{8,4,82},P{2,5,255},P{6,5,58},P{7,5,178},P{2,6,255},P{5,6,4},P{6,6,200},P{7,6,32},P{1,7,255},P{2,7,255},P{3,7,255},P{5,7,118},P{6,7,217},P{7,7,230},P{8,7,243},P{9,7,101},P{4,8,31},P{5,8,201},P{6,8,99},P{7,8,34},P{8,8,43},P{9,8,229},P{4,9,177},P{5,9,60},P{7,9,64},P{8,9,182},P{9,9,55},P{3,10,82},P{4,10,157},P{6,10,96},P{7,10,137},P{8,10,7},P{2,11,12},P{3,11,207},P{4,11,19},P{6,11,255},P{7,11,255},P{8,11,255},P{9,11,255},P{10,11,20}}},
  G{11,{P{0,3,118},P{1,3,237},P{2,3,226},P{3,3,106},P{7,3,17},P{8,3,205},P{9,3,13},P{0,4,90},P{1,4,31},P{2,4,68},P{3,4,231},P{7,4,154},P{8,4,82},P{1,5,255},P{2,5,242},P{3,5,87},P{6,5,58},P{7,5,178},P{0,6,86},P{1,6,28},P{2,6,64},P{3,6,237},P{5,6,4},P{6,6,200},P{7,6,32},P{0,7,127},P{1,7,238},P{2,7,200},P{3,7,85},P{5,7,118},P{6,7,119},P{7,7,110},P{8,7,255},P{4,8,31},P{5,8,201},P{6,8,33},P{7,8,157},P{8,8,255},P{4,9,177},P{5,9,60},P{6,9,165},P{7,9,31},P{8,9,255},P{3,10,82},P{4,10,157},P{6,10,255},P{7,10,255},P{8,10,255},P{9,10,180},P{2,11,12},P{3,11,207},P{4,11,19},P{8,11,255}}},
  G{8,{P{4,5,255},P{3,7,17},P{4,7,237},P{3,8,117},P{4,8,149},P{2,9,110},P{3,9,195},P{4,9,8},P{2,10,225},P{3,10,40},P{2,11,247},P{3,11,8},P{6,11,4},P{2,12,197},P{3,12,112},P{4,12,8},P{5,12,90},P{6,12,214},P{2,13,43},P{3,13,201},P{4,13,246},P{5,13,204},P{6,13,53}}},
  G{9,{P{4,0,193},P{5,0,63},P{4,1,20},P{5,1,171},P{3,3,97},P{4,3,255},P{5,3,104},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{9,{P{4,0,64},P{5,0,192},P{4,1,171},P{5,1,20},P{3,3,97},P{4,3,255},P{5,3,104},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{9,{P{3,0,58},P{4,0,255},P{5,0,66},P{3,1,169},P{4,1,60},P{5,1,169},P{3,3,97},P{4,3,255},P{5,3,104},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{9,{P{3,0,107},P{4,0,215},P{5,0,50},P{6,0,153},P{3,1,152},P{4,1,49},P{5,1,214},P{6,1,107},P{3,3,97},P{4,3,255},P{5,3,104},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{9,{P{3,1,255},P{6,1,255},P{3,3,97},P{4,3,255},P{5,3,104},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{9,{P{3,1,100},P{4,1,242},P{5,1,98},P{3,2,235},P{4,2,59},P{5,2,233},P{3,3,198},P{4,3,255},P{5,3,204},P{3,4,202},P{4,4,143},P{5,4,207},P{2,5,51},P{3,5,216},P{4,5,3},P{5,5,215},P{6,5,56},P{2,6,157},P{3,6,103},P{5,6,101},P{6,6,161},P{1,7,16},P{2,7,225},P{3,7,9},P{5,7,8},P{6,7,224},P{7,7,18},P{1,8,111},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{7,8,114},P{1,9,215},P{2,9,41},P{6,9,38},P{7,9,216},P{8,9,1},P{0,10,66},P{1,10,203},P{7,10,200},P{8,10,67},P{0,11,171},P{1,11,110},P{7,11,107},P{8,11,171}}},
  G{13,{P{3,3,5},P{4,3,224},P{5,3,240},P{6,3,255},P{7,3,255},P{8,3,255},P{9,3,255},P{10,3,255},P{11,3,255},P{3,4,98},P{4,4,192},P{6,4,255},P{2,5,2},P{3,5,218},P{4,5,64},P{6,5,255},P{2,6,89},P{3,6,189},P{6,6,255},P{1,7,1},P{2,7,210},P{3,7,57},P{6,7,255},P{7,7,255},P{8,7,255},P{9,7,255},P{10,7,255},P{1,8,80},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,255},P{6,8,255},P{1,9,203},P{2,9,78},P{6,9,255},P{0,10,71},P{1,10,224},P{2,10,2},P{6,10,255},P{0,11,194},P{1,11,121},P{6,11,255},P{7,11,255},P{8,11,255},P{9,11,255},P{10,11,255},P{11,11,255}}},
  G{9,{P{2,3,72},P{3,3,205},P{4,3,248},P{5,3,234},P{6,3,160},P{7,3,21},P{1,4,61},P{2,4,238},P{3,4,84},P{4,4,7},P{5,4,29},P{6,4,146},P{7,4,156},P{1,5,179},P{2,5,109},P{1,6,225},P{2,6,29},P{1,7,247},P{2,7,7},P{1,8,222},P{2,8,34},P{1,9,165},P{2,9,120},P{1,10,46},P{2,10,241},P{3,10,92},P{4,10,8},P{5,10,21},P{6,10,119},P{7,10,172},P{2,11,71},P{3,11,206},P{4,11,248},P{5,11,233},P{6,11,160},P{7,11,22},P{4,12,203},P{5,12,108},P{4,13,38},P{5,13,228},P{4,14,249},P{5,14,115}}},
  G{9,{P{3,0,193},P{4,0,63},P{3,1,20},P{4,1,171},P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{7,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,255},P{7,11,255}}},
  G{9,{P{4,0,64},P{5,0,192},P{4,1,171},P{5,1,20},P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{7,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,255},P{7,11,255}}},
  G{9,{P{3,0,58},P{4,0,255},P{5,0,66},P{3,1,169},P{4,1,60},P{5,1,169},P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{7,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,255},P{7,11,255}}},
  G{9,{P{3,1,255},P{6,1,255},P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,255},P{5,3,255},P{6,3,255},P{7,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,255},P{5,11,255},P{6,11,255},P{7,11,255}}},
  G{3,{P{0,0,193},P{1,0,63},P{0,1,20},P{1,1,171},P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{1,0,64},P{2,0,192},P{1,1,171},P{2,1,20},P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{0,0,58},P{1,0,255},P{2,0,66},P{0,1,169},P{1,1,60},P{2,1,169},P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{0,1,255},P{3,1,255},P{1,3,255},P{1,4,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{9,{P{1,3,255},P{2,3,255},P{3,3,255},P{4,3,236},P{5,3,174},P{6,3,46},P{1,4,255},P{3,4,2},P{4,4,33},P{5,4,128},P{6,4,239},P{7,4,44},P{1,5,255},P{6,5,126},P{7,5,171},P{1,6,255},P{6,6,25},P{7,6,235},P{0,7,255},P{1,7,255},P{2,7,255},P{3,7,255},P{6,7,8},P{7,7,250},P{1,8,255},P{6,8,42},P{7,8,228},P{1,9,255},P{6,9,139},P{7,9,159},P{1,10,255},P{4,10,27},P{5,10,123},P{6,10,237},P{7,10,36},P{1,11,255},P{2,11,255},P{3,11,255},P{4,11,244},P{5,11,182},P{6,11,45}}},
  G{9,{P{3,0,107},P{4,0,215},P{5,0,50},P{6,0,153},P{3,1,152},P{4,1,49},P{5,1,214},P{6,1,107},P{1,3,250},P{2,3,48},P{7,3,255},P{1,4,248},P{2,4,179},P{3,4,6},P{7,4,255},P{1,5,252},P{2,5,67},P{3,5,133},P{7,5,255},P{1,6,255},P{3,6,146},P{4,6,56},P{7,6,255},P{1,7,255},P{3,7,11},P{4,7,182},P{5,7,9},P{7,7,255},P{1,8,255},P{4,8,61},P{5,8,142},P{7,8,255},P{1,9,255},P{5,9,139},P{6,9,64},P{7,9,251},P{1,10,255},P{5,10,8},P{6,10,183},P{7,10,246},P{1,11,255},P{6,11,54},P{7,11,252}}},
  G{10,{P{4,0,193},P{5,0,63},P{4,1,20},P{5,1,171},P{2,3,56},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,48},P{2,4,239},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,172},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,252},P{2,7,8},P{7,7,8},P{8,7,251},P{1,8,232},P{2,8,35},P{7,8,32},P{8,8,229},P{1,9,167},P{2,9,127},P{7,9,121},P{8,9,162},P{1,10,43},P{2,10,239},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{2,11,53},P{3,11,189},P{4,11,244},P{5,11,244},P{6,11,185},P{7,11,48}}},
  G{10,{P{4,0,64},P{5,0,192},P{4,1,171},P{5,1,20},P{2,3,56},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,48},P{2,4,239},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,172},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,252},P{2,7,8},P{7,7,8},P{8,7,251},P{1,8,232},P{2,8,35},P{7,8,32},P{8,8,229},P{1,9,167},P{2,9,127},P{7,9,121},P{8,9,162},P{1,10,43},P{2,10,239},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{2,11,53},P{3,11,189},P{4,11,244},P{5,11,244},P{6,11,185},P{7,11,48}}},
  G{10,{P{4,0,58},P{5,0,255},P{6,0,66},P{4,1,169},P{5,1,60},P{6,1,169},P{2,3,56},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,48},P{2,4,239},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,172},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,252},P{2,7,8},P{7,7,8},P{8,7,251},P{1,8,232},P{2,8,35},P{7,8,32},P{8,8,229},P{1,9,167},P{2,9,127},P{7,9,121},P{8,9,162},P{1,10,43},P{2,10,239},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{2,11,53},P{3,11,189},P{4,11,244},P{5,11,244},P{6,11,185},P{7,11,48}}},
  G{10,{P{3,0,107},P{4,0,215},P{5,0,50},P{6,0,153},P{3,1,152},P{4,1,49},P{5,1,214},P{6,1,107},P{2,3,56},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,48},P{2,4,239},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,172},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,252},P{2,7,8},P{7,7,8},P{8,7,251},P{1,8,232},P{2,8,35},P{7,8,32},P{8,8,229},P{1,9,167},P{2,9,127},P{7,9,121},P{8,9,162},P{1,10,43},P{2,10,239},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{2,11,53},P{3,11,189},P{4,11,244},P{5,11,244},P{6,11,185},P{7,11,48}}},
  G{10,{P{3,1,255},P{6,1,255},P{2,3,56},P{3,3,189},P{4,3,244},P{5,3,244},P{6,3,185},P{7,3,52},P{1,4,48},P{2,4,239},P{3,4,105},P{4,4,14},P{5,4,14},P{6,4,108},P{7,4,237},P{8,4,45},P{1,5,172},P{2,5,119},P{7,5,124},P{8,5,170},P{1,6,234},P{2,6,32},P{7,6,35},P{8,6,233},P{1,7,252},P{2,7,8},P{7,7,8},P{8,7,251},P{1,8,232},P{2,8,35},P{7,8,32},P{8,8,229},P{1,9,167},P{2,9,127},P{7,9,121},P{8,9,162},P{1,10,43},P{2,10,239},P{3,10,110},P{4,10,14},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{2,11,53},P{3,11,189},P{4,11,244},P{5,11,244},P{6,11,185},P{7,11,48}}},
  G{8,{P{0,4,1},P{1,4,37},P{5,4,34},P{6,4,1},P{0,5,34},P{1,5,224},P{2,5,54},P{4,5,54},P{5,5,227},P{6,5,37},P{1,6,52},P{2,6,223},P{3,6,100},P{4,6,226},P{5,6,58},P{2,7,100},P{3,7,255},P{4,7,108},P{1,8,52},P{2,8,225},P{3,8,104},P{4,8,226},P{5,8,57},P{0,9,36},P{1,9,225},P{2,9,57},P{4,9,54},P{5,9,227},P{6,9,39},P{0,10,3},P{1,10,41},P{5,10,39},P{6,10,2}}},
  G{10,{P{7,2,2},P{8,2,59},P{9,2,5},P{2,3,55},P{3,3,189},P{4,3,244},P{5,3,237},P{6,3,180},P{7,3,150},P{8,3,157},P{1,4,47},P{2,4,238},P{3,4,105},P{4,4,14},P{5,4,17},P{6,4,139},P{7,4,251},P{8,4,47},P{1,5,171},P{2,5,119},P{5,5,20},P{6,5,187},P{7,5,110},P{8,5,164},P{1,6,234},P{2,6,32},P{4,6,4},P{5,6,181},P{6,6,49},P{7,6,32},P{8,6,234},P{1,7,249},P{2,7,7},P{4,7,143},P{5,7,105},P{7,7,8},P{8,7,251},P{1,8,222},P{2,8,31},P{3,8,93},P{4,8,168},P{7,8,32},P{8,8,229},P{1,9,171},P{2,9,166},P{3,9,209},P{4,9,13},P{7,9,121},P{8,9,162},P{1,10,53},P{2,10,255},P{3,10,142},P{4,10,18},P{5,10,13},P{6,10,106},P{7,10,235},P{8,10,39},P{1,11,154},P{2,11,150},P{3,11,184},P{4,11,239},P{5,11,244},P{6,11,185},P{7,11,48},P{0,12,4},P{1,12,55},P{2,12,1}}},
  G{9,{P{4,0,193},P{5,0,63},P{4,1,20},P{5,1,171},P{1,3,255},P{7,3,255},P{1,4,255},P{7,4,255},P{1,5,255},P{7,5,255},P{1,6,255},P{7,6,255},P{1,7,255},P{7,7,255},P{1,8,254},P{2,8,4},P{6,8,6},P{7,8,253},P{1,9,227},P{2,9,44},P{6,9,55},P{7,9,219},P{1,10,91},P{2,10,196},P{3,10,42},P{4,10,8},P{5,10,55},P{6,10,210},P{7,10,84},P{2,11,70},P{3,11,208},P{4,11,247},P{5,11,215},P{6,11,102}}},
  G{9,{P{4,0,64},P{5,0,192},P{4,1,171},P{5,1,20},P{1,3,255},P{7,3,255},P{1,4,255},P{7,4,255},P{1,5,255},P{7,5,255},P{1,6,255},P{7,6,255},P{1,7,255},P{7,7,255},P{1,8,254},P{2,8,4},P{6,8,6},P{7,8,253},P{1,9,227},P{2,9,44},P{6,9,55},P{7,9,219},P{1,10,91},P{2,10,196},P{3,10,42},P{4,10,8},P{5,10,55},P{6,10,210},P{7,10,84},P{2,11,70},P{3,11,208},P{4,11,247},P{5,11,215},P{6,11,102}}},
  G{9,{P{3,0,58},P{4,0,255},P{5,0,66},P{3,1,169},P{4,1,60},P{5,1,169},P{1,3,255},P{7,3,255},P{1,4,255},P{7,4,255},P{1,5,255},P{7,5,255},P{1,6,255},P{7,6,255},P{1,7,255},P{7,7,255},P{1,8,254},P{2,8,4},P{6,8,6},P{7,8,253},P{1,9,227},P{2,9,44},P{6,9,55},P{7,9,219},P{1,10,91},P{2,10,196},P{3,10,42},P{4,10,8},P{5,10,55},P{6,10,210},P{7,10,84},P{2,11,70},P{3,11,208},P{4,11,247},P{5,11,215},P{6,11,102}}},
  G{9,{P{3,1,255},P{6,1,255},P{1,3,255},P{7,3,255},P{1,4,255},P{7,4,255},P{1,5,255},P{7,5,255},P{1,6,255},P{7,6,255},P{1,7,255},P{7,7,255},P{1,8,254},P{2,8,4},P{6,8,6},P{7,8,253},P{1,9,227},P{2,9,44},P{6,9,55},P{7,9,219},P{1,10,91},P{2,10,196},P{3,10,42},P{4,10,8},P{5,10,55},P{6,10,210},P{7,10,84},P{2,11,70},P{3,11,208},P{4,11,247},P{5,11,215},P{6,11,102}}},
  G{9,{P{4,0,64},P{5,0,192},P{4,1,171},P{5,1,20},P{1,3,175},P{2,3,68},P{6,3,68},P{7,3,174},P{1,4,28},P{2,4,202},P{3,4,5},P{5,4,5},P{6,4,200},P{7,4,27},P{2,5,109},P{3,5,119},P{5,5,115},P{6,5,108},P{2,6,2},P{3,6,190},P{4,6,52},P{5,6,187},P{6,6,2},P{3,7,47},P{4,7,246},P{5,7,47},P{4,8,255},P{4,9,255},P{4,10,255},P{4,11,255}}},
  G{9,{P{1,3,255},P{1,4,255},P{1,5,255},P{2,5,255},P{3,5,255},P{4,5,254},P{5,5,231},P{6,5,166},P{7,5,27},P{1,6,255},P{4,6,1},P{5,6,23},P{6,6,138},P{7,6,190},P{1,7,255},P{6,7,15},P{7,7,242},P{1,8,255},P{5,8,24},P{6,8,145},P{7,8,179},P{1,9,255},P{2,9,255},P{3,9,255},P{4,9,255},P{5,9,229},P{6,9,159},P{7,9,21},P{1,10,255},P{1,11,255}}},
  G{9,{P{1,3,26},P{2,3,168},P{3,3,233},P{4,3,245},P{5,3,202},P{6,3,67},P{1,4,182},P{2,4,81},P{3,4,20},P{4,4,7},P{5,4,80},P{6,4,233},P{1,5,243},P{2,5,12},P{5,5,107},P{6,5,186},P{1,6,255},P{4,6,113},P{5,6,150},P{6,6,10},P{1,7,255},P{4,7,229},P{5,7,78},P{1,8,255},P{4,8,54},P{5,8,205},P{6,8,184},P{7,8,30},P{1,9,255},P{5,9,1},P{6,9,109},P{7,9,203},P{1,10,255},P{4,10,63},P{5,10,7},P{6,10,51},P{7,10,237},P{1,11,255},P{4,11,206},P{5,11,248},P{6,11,227},P{7,11,88}}},
  G{7,{P{3,2,183},P{4,2,62},P{3,3,13},P{4,3,155},P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{7,{P{3,2,13},P{4,2,208},P{5,2,105},P{3,3,164},P{4,3,74},P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{7,{P{2,2,58},P{3,2,255},P{4,2,65},P{2,3,168},P{3,3,60},P{4,3,167},P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{7,{P{2,2,107},P{3,2,215},P{4,2,50},P{5,2,153},P{2,3,152},P{3,3,49},P{4,3,214},P{5,3,107},P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{7,{P{2,3,255},P{4,3,255},P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{7,{P{2,1,100},P{3,1,242},P{4,1,98},P{2,2,235},P{3,2,59},P{4,2,233},P{2,3,101},P{3,3,244},P{4,3,100},P{1,5,66},P{2,5,211},P{3,5,247},P{4,5,216},P{5,5,70},P{1,6,212},P{2,6,74},P{3,6,7},P{4,6,97},P{5,6,222},P{4,7,6},P{5,7,255},P{1,8,64},P{2,8,203},P{3,8,243},P{4,8,253},P{5,8,255},P{1,9,231},P{2,9,91},P{3,9,13},P{4,9,12},P{5,9,255},P{1,10,191},P{2,10,52},P{3,10,19},P{4,10,129},P{5,10,234},P{6,10,36},P{1,11,51},P{2,11,238},P{3,11,223},P{4,11,68},P{5,11,128},P{6,11,245}}},
  G{12,{P{1,5,45},P{2,5,186},P{3,5,238},P{4,5,248},P{5,5,199},P{6,5,65},P{7,5,185},P{8,5,248},P{9,5,190},P{10,5,16},P{1,6,203},P{2,6,95},P{3,6,10},P{4,6,11},P{5,6,117},P{6,6,255},P{7,6,136},P{8,6,12},P{9,6,137},P{10,6,149},P{5,7,7},P{6,7,255},P{7,7,24},P{9,7,22},P{10,7,223},P{1,8,46},P{2,8,180},P{3,8,232},P{4,8,248},P{5,8,253},P{6,8,255},P{7,8,255},P{8,8,255},P{9,8,255},P{10,8,248},P{1,9,220},P{2,9,106},P{3,9,23},P{4,9,5},P{5,9,14},P{6,9,255},P{7,9,13},P{1,10,186},P{2,10,63},P{3,10,6},P{4,10,40},P{5,10,163},P{6,10,181},P{7,10,125},P{8,10,10},P{9,10,94},P{10,10,189},P{1,11,34},P{2,11,217},P{3,11,246},P{4,11,201},P{5,11,59},P{6,11,19},P{7,11,186},P{8,11,246},P{9,11,211},P{10,11,61}}},
  G{7,{P{1,5,20},P{2,5,181},P{3,5,246},P{4,5,208},P{5,5,53},P{1,6,160},P{2,6,135},P{3,6,12},P{4,6,81},P{5,6,205},P{1,7,233},P{2,7,22},P{1,8,250},P{2,8,4},P{1,9,230},P{2,9,28},P{1,10,156},P{2,10,146},P{3,10,16},P{4,10,86},P{5,10,212},P{1,11,19},P{2,11,184},P{3,11,248},P{4,11,211},P{5,11,56},P{3,12,203},P{4,12,108},P{3,13,38},P{4,13,228},P{3,14,249},P{4,14,115}}},
  G{7,{P{3,2,183},P{4,2,62},P{3,3,13},P{4,3,155},P{1,5,25},P{2,5,186},P{3,5,245},P{4,5,191},P{5,5,16},P{1,6,169},P{2,6,141},P{3,6,12},P{4,6,136},P{5,6,149},P{1,7,236},P{2,7,24},P{4,7,22},P{5,7,223},P{1,8,250},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,248},P{1,9,227},P{2,9,13},P{1,10,155},P{2,10,129},P{3,10,10},P{4,10,99},P{5,10,189},P{1,11,21},P{2,11,179},P{3,11,243},P{4,11,213},P{5,11,61}}},
  G{7,{P{3,2,13},P{4,2,208},P{5,2,105},P{3,3,164},P{4,3,74},P{1,5,25},P{2,5,186},P{3,5,245},P{4,5,191},P{5,5,16},P{1,6,169},P{2,6,141},P{3,6,12},P{4,6,136},P{5,6,149},P{1,7,236},P{2,7,24},P{4,7,22},P{5,7,223},P{1,8,250},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,248},P{1,9,227},P{2,9,13},P{1,10,155},P{2,10,129},P{3,10,10},P{4,10,99},P{5,10,189},P{1,11,21},P{2,11,179},P{3,11,243},P{4,11,213},P{5,11,61}}},
  G{7,{P{2,2,58},P{3,2,255},P{4,2,65},P{2,3,168},P{3,3,60},P{4,3,167},P{1,5,25},P{2,5,186},P{3,5,245},P{4,5,191},P{5,5,16},P{1,6,169},P{2,6,141},P{3,6,12},P{4,6,136},P{5,6,149},P{1,7,236},P{2,7,24},P{4,7,22},P{5,7,223},P{1,8,250},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,248},P{1,9,227},P{2,9,13},P{1,10,155},P{2,10,129},P{3,10,10},P{4,10,99},P{5,10,189},P{1,11,21},P{2,11,179},P{3,11,243},P{4,11,213},P{5,11,61}}},
  G{7,{P{2,3,255},P{4,3,255},P{1,5,25},P{2,5,186},P{3,5,245},P{4,5,191},P{5,5,16},P{1,6,169},P{2,6,141},P{3,6,12},P{4,6,136},P{5,6,149},P{1,7,236},P{2,7,24},P{4,7,22},P{5,7,223},P{1,8,250},P{2,8,255},P{3,8,255},P{4,8,255},P{5,8,248},P{1,9,227},P{2,9,13},P{1,10,155},P{2,10,129},P{3,10,10},P{4,10,99},P{5,10,189},P{1,11,21},P{2,11,179},P{3,11,243},P{4,11,213},P{5,11,61}}},
  G{3,{P{0,2,183},P{1,2,62},P{0,3,13},P{1,3,155},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{1,2,13},P{2,2,208},P{3,2,105},P{1,3,164},P{2,3,74},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{0,2,58},P{1,2,255},P{2,2,65},P{0,3,168},P{1,3,60},P{2,3,167},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{3,{P{0,3,255},P{2,3,255},P{1,5,255},P{1,6,255},P{1,7,255},P{1,8,255},P{1,9,255},P{1,10,255},P{1,11,255}}},
  G{7,{P{1,3,1},P{2,3,145},P{3,3,109},P{4,3,159},P{5,3,1},P{2,4,127},P{3,4,223},P{4,4,83},P{2,5,120},P{3,5,8},P{4,5,193},P{5,5,20},P{1,6,36},P{2,6,196},P{3,6,244},P{4,6,230},P{5,6,135},P{1,7,186},P{2,7,117},P{3,7,12},P{4,7,116},P{5,7,210},P{1,8,243},P{2,8,11},P{4,8,13},P{5,8,245},P{1,9,241},P{2,9,13},P{4,9,14},P{5,9,237},P{1,10,185},P{2,10,117},P{3,10,10},P{4,10,121},P{5,10,178},P{1,11,38},P{2,11,199},P{3,11,244},P{4,11,196},P{5,11,34}}},
  G{7,{P{2,2,107},P{3,2,215},P{4,2,50},P{5,2,153},P{2,3,152},P{3,3,49},P{4,3,214},P{5,3,107},P{0,5,7},P{1,5,236},P{2,5,93},P{3,5,233},P{4,5,234},P{5,5,90},P{0,6,1},P{1,6,250},P{2,6,139},P{3,6,13},P{4,6,85},P{5,6,223},P{1,7,255},P{2,7,20},P{4,7,6},P{5,7,253},P{1,8,255},P{5,8,255},P{1,9,255},P{5,9,255},P{1,10,255},P{5,10,255},P{1,11,255},P{5,11,255}}},
  G{7,{P{3,2,183},P{4,2,62},P{3,3,13},P{4,3,155},P{1,5,27},P{2,5,191},P{3,5,243},P{4,5,194},P{5,5,31},P{1,6,164},P{2,6,136},P{3,6,13},P{4,6,131},P{5,6,167},P{1,7,230},P{2,7,23},P{4,7,22},P{5,7,229},P{1,8,249},P{2,8,3},P{4,8,3},P{5,8,249},P{1,9,228},P{2,9,22},P{4,9,22},P{5,9,228},P{1,10,160},P{2,10,132},P{3,10,11},P{4,10,137},P{5,10,161},P{1,11,26},P{2,11,192},P{3,11,243},P{4,11,188},P{5,11,26}}},
  G{7,{P{3,2,13},P{4,2,208},P{5,2,105},P{3,3,164},P{4,3,74},P{1,5,27},P{2,5,191},P{3,5,243},P{4,5,194},P{5,5,31},P{1,6,164},P{2,6,136},P{3,6,13},P{4,6,131},P{5,6,167},P{1,7,230},P{2,7,23},P{4,7,22},P{5,7,229},P{1,8,249},P{2,8,3},P{4,8,3},P{5,8,249},P{1,9,228},P{2,9,22},P{4,9,22},P{5,9,228},P{1,10,160},P{2,10,132},P{3,10,11},P{4,10,137},P{5,10,161},P{1,11,26},P{2,11,192},P{3,11,243},P{4,11,188},P{5,11,26}}},
  G{7,{P{2,2,58},P{3,2,255},P{4,2,65},P{2,3,168},P{3,3,60},P{4,3,167},P{1,5,27},P{2,5,191},P{3,5,243},P{4,5,194},P{5,5,31},P{1,6,164},P{2,6,136},P{3,6,13},P{4,6,131},P{5,6,167},P{1,7,230},P{2,7,23},P{4,7,22},P{5,7,229},P{1,8,249},P{2,8,3},P{4,8,3},P{5,8,249},P{1,9,228},P{2,9,22},P{4,9,22},P{5,9,228},P{1,10,160},P{2,10,132},P{3,10,11},P{4,10,137},P{5,10,161},P{1,11,26},P{2,11,192},P{3,11,243},P{4,11,188},P{5,11,26}}},
  G{7,{P{2,2,107},P{3,2,215},P{4,2,50},P{5,2,153},P{2,3,152},P{3,3,49},P{4,3,214},P{5,3,107},P{1,5,27},P{2,5,191},P{3,5,243},P{4,5,194},P{5,5,31},P{1,6,164},P{2,6,136},P{3,6,13},P{4,6,131},P{5,6,167},P{1,7,230},P{2,7,23},P{4,7,22},P{5,7,229},P{1,8,249},P{2,8,3},P{4,8,3},P{5,8,249},P{1,9,228},P{2,9,22},P{4,9,22},P{5,9,228},P{1,10,160},P{2,10,132},P{3,10,11},P{4,10,137},P{5,10,161},P{1,11,26},P{2,11,192},P{3,11,243},P{4,11,188},P{5,11,26}}},
  G{7,{P{2,3,255},P{4,3,255},P{1,5,27},P{2,5,191},P{3,5,243},P{4,5,194},P{5,5,31},P{1,6,164},P{2,6,136},P{3,6,13},P{4,6,131},P{5,6,167},P{1,7,230},P{2,7,23},P{4,7,22},P{5,7,229},P{1,8,249},P{2,8,3},P{4,8,3},P{5,8,249},P{1,9,228},P{2,9,22},P{4,9,22},P{5,9,228},P{1,10,160},P{2,10,132},P{3,10,11},P{4,10,137},P{5,10,161},P{1,11,26},P{2,11,192},P{3,11,243},P{4,11,188},P{5,11,26}}},
  G{7,{P{3,4,255},P{0,7,255},P{1,7,255},P{2,7,255},P{3,7,255},P{4,7,255},P{5,7,255},P{6,7,255},P{3,10,255}}},
  G{7,{P{5,4,8},P{6,4,10},P{1,5,27},P{2,5,191},P{3,5,244},P{4,5,195},P{5,5,193},P{6,5,42},P{1,6,164},P{2,6,136},P{3,6,12},P{4,6,171},P{5,6,177},P{1,7,230},P{2,7,23},P{3,7,55},P{4,7,177},P{5,7,233},P{1,8,250},P{2,8,20},P{3,8,186},P{4,8,16},P{5,8,249},P{1,9,233},P{2,9,182},P{3,9,48},P{4,9,22},P{5,9,228},P{1,10,177},P{2,10,169},P{3,10,11},P{4,10,137},P{5,10,161},P{0,11,49},P{1,11,190},P{2,11,196},P{3,11,244},P{4,11,188},P{5,11,26},P{0,12,16},P{1,12,10}}},
  G{7,{P{3,2,183},P{4,2,62},P{3,3,13},P{4,3,155},P{1,5,255},P{5,5,255},P{1,6,255},P{5,6,255},P{1,7,255},P{5,7,255},P{1,8,255},P{5,8,255},P{1,9,253},P{2,9,5},P{4,9,23},P{5,9,255},P{1,10,228},P{2,10,82},P{3,10,13},P{4,10,140},P{5,10,251},P{1,11,95},P{2,11,235},P{3,11,235},P{4,11,92},P{5,11,238},P{6,11,1}}},
  G{7,{P{3,2,13},P{4,2,208},P{5,2,105},P{3,3,164},P{4,3,74},P{1,5,255},P{5,5,255},P{1,6,255},P{5,6,255},P{1,7,255},P{5,7,255},P{1,8,255},P{5,8,255},P{1,9,253},P{2,9,5},P{4,9,23},P{5,9,255},P{1,10,228},P{2,10,82},P{3,10,13},P{4,10,140},P{5,10,251},P{1,11,95},P{2,11,235},P{3,11,235},P{4,11,92},P{5,11,238},P{6,11,1}}},
  G{7,{P{2,2,58},P{3,2,255},P{4,2,65},P{2,3,168},P{3,3,60},P{4,3,167},P{1,5,255},P{5,5,255},P{1,6,255},P{5,6,255},P{1,7,255},P{5,7,255},P{1,8,255},P{5,8,255},P{1,9,253},P{2,9,5},P{4,9,23},P{5,9,255},P{1,10,228},P{2,10,82},P{3,10,13},P{4,10,140},P{5,10,251},P{1,11,95},P{2,11,235},P{3,11,235},P{4,11,92},P{5,11,238},P{6,11,1}}},
  G{7,{P{2,3,255},P{4,3,255},P{1,5,255},P{5,5,255},P{1,6,255},P{5,6,255},P{1,7,255},P{5,7,255},P{1,8,255},P{5,8,255},P{1,9,253},P{2,9,5},P{4,9,23},P{5,9,255},P{1,10,228},P{2,10,82},P{3,10,13},P{4,10,140},P{5,10,251},P{1,11,95},P{2,11,235},P{3,11,235},P{4,11,92},P{5,11,238},P{6,11,1}}},
  G{7,{P{3,2,13},P{4,2,208},P{5,2,105},P{3,3,164},P{4,3,74},P{-1,5,17},P{0,5,221},P{1,5,8},P{5,5,10},P{6,5,220},P{7,5,17},P{0,6,144},P{1,6,102},P{5,6,117},P{6,6,142},P{0,7,31},P{1,7,212},P{2,7,1},P{4,7,11},P{5,7,229},P{6,7,29},P{1,8,166},P{2,8,80},P{4,8,118},P{5,8,161},P{1,9,49},P{2,9,233},P{3,9,20},P{4,9,231},P{5,9,44},P{2,10,188},P{3,10,196},P{4,10,181},P{2,11,71},P{3,11,255},P{4,11,63},P{2,12,86},P{3,12,200},P{0,13,4},P{1,13,61},P{2,13,232},P{3,13,65},P{0,14,249},P{1,14,227},P{2,14,96}}},
  G{7,{P{1,3,255},P{1,4,255},P{1,5,255},P{2,5,84},P{3,5,240},P{4,5,221},P{5,5,44},P{1,6,254},P{2,6,132},P{3,6,13},P{4,6,127},P{5,6,181},P{1,7,255},P{2,7,24},P{4,7,21},P{5,7,236},P{1,8,255},P{2,8,3},P{4,8,4},P{5,8,250},P{1,9,255},P{2,9,23},P{4,9,21},P{5,9,235},P{1,10,254},P{2,10,125},P{3,10,13},P{4,10,127},P{5,10,177},P{1,11,254},P{2,11,79},P{3,11,236},P{4,11,219},P{5,11,39},P{1,12,255},P{1,13,255},P{1,14,255}}},
  G{7,{P{2,3,255},P{4,3,255},P{-1,5,17},P{0,5,221},P{1,5,8},P{5,5,10},P{6,5,220},P{7,5,17},P{0,6,144},P{1,6,102},P{5,6,117},P{6,6,142},P{0,7,31},P{1,7,212},P{2,7,1},P{4,7,11},P{5,7,229},P{6,7,29},P{1,8,166},P{2,8,80},P{4,8,118},P{5,8,161},P{1,9,49},P{2,9,233},P{3,9,20},P{4,9,231},P{5,9,44},P{2,10,188},P{3,10,196},P{4,10,181},P{2,11,71},P{3,11,255},P{4,11,63},P{2,12,86},P{3,12,200},P{0,13,4},P{1,13,61},P{2,13,232},P{3,13,65},P{0,14,249},P{1,14,227},P{2,14,96}}}
 }}
);

#else

// NO_INITIALIZER_LISTS (Visual Studio 2012)

ClientSide::Font Global::graphFont;

// This has to be called specially before graphFont is used.
void Global::initializeGraphFont() {
  Global::graphFont.glyphs.reserve (256);
  Global::graphFont.height = 12;
  Global::graphFont.oughtHeight = 9;
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,251},{1,4,242},{1,5,233},{1,6,224},{1,7,215},{1,8,206},{1,9,196},{1,11,255}};
      g.pixels.assign (p, p+8);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{1,3,246},{3,3,246},{1,4,226},{3,4,226},{1,5,206},{3,5,206}};
      g.pixels.assign (p, p+6);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,152},{3,3,125},{5,3,151},{6,3,124},{2,4,144},{3,4,71},{5,4,141},{6,4,68},{0,5,108},{1,5,255},{2,5,255},{3,5,255},{4,5,255},{5,5,255},{6,5,255},{7,5,96},{1,6,6},{2,6,201},{4,6,5},{5,6,195},{1,7,84},{2,7,165},{4,7,80},{5,7,163},{1,8,180},{2,8,111},{4,8,176},{5,8,110},{0,9,188},{1,9,255},{2,9,255},{3,9,255},{4,9,255},{5,9,255},{6,9,255},{7,9,12},{1,10,172},{2,10,7},{4,10,169},{5,10,9},{1,11,180},{4,11,177}};
      g.pixels.assign (p, p+42);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,255},{1,3,74},{2,3,226},{3,3,255},{4,3,225},{5,3,64},{1,4,223},{2,4,66},{3,4,255},{4,4,81},{5,4,212},{1,5,244},{2,5,11},{3,5,255},{4,5,1},{5,5,37},{1,6,178},{2,6,137},{3,6,255},{4,6,4},{1,7,15},{2,7,140},{3,7,255},{4,7,227},{5,7,63},{3,8,255},{4,8,83},{5,8,215},{1,9,99},{2,9,5},{3,9,255},{4,9,7},{5,9,248},{1,10,202},{2,10,89},{3,10,255},{4,10,79},{5,10,209},{1,11,57},{2,11,221},{3,11,255},{4,11,219},{5,11,60},{3,12,255}};
      g.pixels.assign (p, p+44);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 12;
    {
      const ClientSide::Pixel p[] = {{1,3,82},{2,3,231},{3,3,233},{4,3,80},{7,3,25},{8,3,213},{9,3,13},{1,4,224},{2,4,63},{3,4,61},{4,4,223},{7,4,167},{8,4,84},{1,5,249},{2,5,5},{3,5,6},{4,5,249},{6,5,68},{7,5,183},{1,6,216},{2,6,64},{3,6,68},{4,6,215},{5,6,7},{6,6,209},{7,6,35},{1,7,78},{2,7,233},{3,7,232},{4,7,76},{5,7,126},{6,7,125},{7,7,85},{8,7,233},{9,7,232},{10,7,82},{4,8,35},{5,8,209},{6,8,7},{7,8,226},{8,8,63},{9,8,61},{10,8,223},{4,9,183},{5,9,68},{7,9,250},{8,9,5},{9,9,6},{10,9,249},{3,10,85},{4,10,166},{7,10,218},{8,10,64},{9,10,68},{10,10,213},{2,11,13},{3,11,214},{4,11,24},{7,11,81},{8,11,233},{9,11,231},{10,11,73}};
      g.pixels.assign (p, p+62);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{2,3,92},{3,3,231},{4,3,236},{5,3,67},{2,4,236},{3,4,63},{4,4,55},{5,4,228},{2,5,241},{3,5,11},{4,5,38},{5,5,205},{2,6,179},{3,6,136},{4,6,177},{5,6,40},{1,7,68},{2,7,226},{3,7,236},{4,7,10},{6,7,13},{7,7,142},{1,8,207},{2,8,57},{3,8,188},{4,8,127},{6,8,115},{7,8,130},{1,9,247},{2,9,9},{3,9,41},{4,9,243},{5,9,126},{6,9,206},{7,9,12},{1,10,209},{2,10,102},{3,10,11},{4,10,173},{5,10,255},{6,10,102},{7,10,8},{1,11,57},{2,11,212},{3,11,247},{4,11,176},{5,11,102},{6,11,223},{7,11,244}};
      g.pixels.assign (p, p+49);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 2;
    {
      const ClientSide::Pixel p[] = {{1,3,246},{1,4,226},{1,5,206}};
      g.pixels.assign (p, p+3);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{2,3,49},{3,3,206},{4,3,10},{1,4,3},{2,4,212},{3,4,48},{1,5,88},{2,5,174},{1,6,171},{2,6,88},{1,7,225},{2,7,31},{1,8,247},{2,8,9},{1,9,247},{2,9,9},{1,10,225},{2,10,31},{1,11,171},{2,11,87},{1,12,88},{2,12,172},{1,13,3},{2,13,213},{3,13,46},{2,14,50},{3,14,205},{4,14,10}};
      g.pixels.assign (p, p+28);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{-1,3,11},{0,3,208},{1,3,47},{0,4,51},{1,4,210},{2,4,2},{1,5,178},{2,5,83},{1,6,93},{2,6,168},{1,7,35},{2,7,225},{1,8,11},{2,8,246},{1,9,11},{2,9,246},{1,10,35},{2,10,225},{1,11,92},{2,11,168},{1,12,176},{2,12,84},{0,13,49},{1,13,211},{2,13,2},{-1,14,10},{0,14,206},{1,14,48}};
      g.pixels.assign (p, p+28);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{2,3,244},{0,4,113},{1,4,45},{2,4,221},{3,4,49},{4,4,113},{0,5,97},{1,5,183},{2,5,254},{3,5,177},{4,5,95},{0,6,11},{1,6,193},{2,6,127},{3,6,167},{4,6,1},{0,7,111},{1,7,105},{3,7,95},{4,7,73}};
      g.pixels.assign (p, p+20);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{3,4,184},{3,5,255},{3,6,255},{0,7,176},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,176},{3,8,255},{3,9,255},{3,10,184}};
      g.pixels.assign (p, p+13);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{2,11,255},{1,12,25},{2,12,240},{1,13,165},{2,13,97}};
      g.pixels.assign (p, p+5);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,8,255},{2,8,255}};
      g.pixels.assign (p, p+2);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{2,11,255}};
      g.pixels.assign (p, p+1);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{2,3,12},{3,3,200},{2,4,99},{3,4,125},{2,5,196},{3,5,39},{1,6,39},{2,6,207},{1,7,136},{2,7,121},{0,8,3},{1,8,229},{2,8,35},{0,9,76},{1,9,204},{0,10,174},{1,10,117},{-1,11,21},{0,11,249},{1,11,31}};
      g.pixels.assign (p, p+20);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,16},{2,3,183},{3,3,241},{4,3,183},{5,3,15},{1,4,137},{2,4,121},{3,4,14},{4,4,120},{5,4,134},{1,5,213},{2,5,37},{4,5,39},{5,5,209},{1,6,245},{2,6,9},{4,6,10},{5,6,243},{1,7,253},{2,7,1},{4,7,2},{5,7,253},{1,8,244},{2,8,9},{4,8,11},{5,8,242},{1,9,210},{2,9,38},{4,9,41},{5,9,206},{1,10,132},{2,10,123},{3,10,14},{4,10,126},{5,10,129},{1,11,14},{2,11,183},{3,11,241},{4,11,179},{5,11,12}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,88},{3,3,254},{1,4,59},{2,4,134},{3,4,255},{1,5,113},{3,5,255},{3,6,255},{3,7,255},{3,8,255},{3,9,255},{3,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255}};
      g.pixels.assign (p, p+17);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,33},{2,3,191},{3,3,244},{4,3,209},{5,3,55},{1,4,190},{2,4,122},{3,4,10},{4,4,101},{5,4,208},{1,5,98},{2,5,7},{4,5,13},{5,5,244},{4,6,81},{5,6,188},{3,7,40},{4,7,224},{5,7,47},{2,8,45},{3,8,224},{4,8,73},{1,9,26},{2,9,225},{3,9,75},{1,10,165},{2,10,131},{1,11,254},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,36}};
      g.pixels.assign (p, p+33);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,38},{2,3,196},{3,3,245},{4,3,206},{5,3,53},{1,4,193},{2,4,115},{3,4,10},{4,4,103},{5,4,210},{1,5,92},{2,5,5},{4,5,11},{5,5,246},{3,6,4},{4,6,107},{5,6,180},{3,7,255},{4,7,228},{5,7,36},{3,8,4},{4,8,106},{5,8,194},{1,9,117},{2,9,7},{4,9,11},{5,9,246},{1,10,194},{2,10,119},{3,10,10},{4,10,104},{5,10,204},{1,11,39},{2,11,198},{3,11,245},{4,11,208},{5,11,53}};
      g.pixels.assign (p, p+37);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,3,92},{4,3,255},{2,4,1},{3,4,190},{4,4,255},{2,5,78},{3,5,137},{4,5,255},{2,6,197},{3,6,33},{4,6,255},{1,7,64},{2,7,180},{4,7,255},{1,8,184},{2,8,72},{4,8,255},{1,9,255},{2,9,255},{3,9,255},{4,9,255},{5,9,255},{6,9,192},{4,10,255},{4,11,255}};
      g.pixels.assign (p, p+25);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,188},{2,3,255},{3,3,255},{4,3,255},{5,3,148},{1,4,205},{2,4,35},{1,5,221},{2,5,17},{1,6,237},{2,6,185},{3,6,242},{4,6,191},{5,6,30},{1,7,164},{2,7,65},{3,7,13},{4,7,143},{5,7,177},{4,8,18},{5,8,240},{1,9,49},{2,9,2},{4,9,20},{5,9,233},{1,10,203},{2,10,108},{3,10,12},{4,10,141},{5,10,166},{1,11,49},{2,11,208},{3,11,244},{4,11,185},{5,11,25}};
      g.pixels.assign (p, p+35);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,2},{2,3,149},{3,3,244},{4,3,221},{5,3,65},{1,4,104},{2,4,138},{3,4,17},{4,4,86},{5,4,199},{1,5,194},{2,5,45},{1,6,238},{2,6,87},{3,6,233},{4,6,207},{5,6,40},{1,7,252},{2,7,133},{3,7,13},{4,7,129},{5,7,186},{1,8,239},{2,8,38},{4,8,15},{5,8,241},{1,9,208},{2,9,51},{4,9,16},{5,9,237},{1,10,125},{2,10,142},{3,10,15},{4,10,128},{5,10,172},{1,11,11},{2,11,178},{3,11,246},{4,11,195},{5,11,30}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{4,4,104},{5,4,169},{3,5,18},{4,5,223},{5,5,26},{3,6,131},{4,6,128},{2,7,11},{3,7,222},{4,7,16},{2,8,87},{3,8,161},{2,9,165},{3,9,83},{2,10,214},{3,10,38},{2,11,242},{3,11,12}};
      g.pixels.assign (p, p+23);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,47},{2,3,201},{3,3,244},{4,3,201},{5,3,48},{1,4,206},{2,4,97},{3,4,10},{4,4,100},{5,4,205},{1,5,246},{2,5,7},{4,5,6},{5,5,246},{1,6,184},{2,6,103},{3,6,11},{4,6,96},{5,6,181},{1,7,38},{2,7,235},{3,7,255},{4,7,235},{5,7,39},{1,8,188},{2,8,102},{3,8,11},{4,8,99},{5,8,192},{1,9,247},{2,9,8},{4,9,8},{5,9,246},{1,10,209},{2,10,100},{3,10,9},{4,10,98},{5,10,206},{1,11,51},{2,11,206},{3,11,246},{4,11,206},{5,11,51}};
      g.pixels.assign (p, p+43);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,31},{2,3,191},{3,3,245},{4,3,184},{5,3,14},{1,4,179},{2,4,129},{3,4,15},{4,4,126},{5,4,138},{1,5,240},{2,5,15},{4,5,49},{5,5,213},{1,6,240},{2,6,14},{4,6,41},{5,6,245},{1,7,182},{2,7,127},{3,7,13},{4,7,144},{5,7,254},{1,8,38},{2,8,208},{3,8,238},{4,8,120},{5,8,248},{4,9,52},{5,9,226},{1,10,206},{2,10,82},{3,10,16},{4,10,145},{5,10,143},{1,11,70},{2,11,225},{3,11,245},{4,11,154},{5,11,6}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{2,5,255},{2,11,255}};
      g.pixels.assign (p, p+2);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{2,5,255},{2,11,255},{1,12,25},{2,12,238},{1,13,165},{2,13,95}};
      g.pixels.assign (p, p+6);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{5,4,73},{6,4,195},{2,5,2},{3,5,85},{4,5,205},{5,5,183},{6,5,66},{1,6,97},{2,6,210},{3,6,159},{4,6,43},{1,7,233},{2,7,50},{1,8,101},{2,8,212},{3,8,158},{4,8,43},{2,9,3},{3,9,88},{4,9,207},{5,9,182},{6,9,66},{5,10,74},{6,10,195}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{1,6,255},{2,6,255},{3,6,255},{4,6,255},{5,6,255},{6,6,255},{1,9,255},{2,9,255},{3,9,255},{4,9,255},{5,9,255},{6,9,255}};
      g.pixels.assign (p, p+12);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{1,4,194},{2,4,72},{1,5,66},{2,5,183},{3,5,204},{4,5,84},{5,5,2},{3,6,43},{4,6,158},{5,6,209},{6,6,96},{5,7,49},{6,7,232},{3,8,42},{4,8,157},{5,8,212},{6,8,101},{1,9,66},{2,9,182},{3,9,206},{4,9,87},{5,9,2},{1,10,195},{2,10,74}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,53},{2,3,204},{3,3,245},{4,3,201},{5,3,42},{1,4,217},{2,4,90},{3,4,9},{4,4,114},{5,4,193},{1,5,4},{4,5,11},{5,5,245},{4,6,41},{5,6,223},{3,7,8},{4,7,194},{5,7,108},{3,8,151},{4,8,125},{3,9,239},{4,9,21},{3,11,255}};
      g.pixels.assign (p, p+23);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 13;
    {
      const ClientSide::Pixel p[] = {{3,3,9},{4,3,105},{5,3,198},{6,3,236},{7,3,248},{8,3,217},{9,3,139},{10,3,20},{2,4,31},{3,4,202},{4,4,106},{5,4,8},{7,4,7},{8,4,39},{9,4,110},{10,4,214},{11,4,27},{1,5,7},{2,5,194},{3,5,64},{10,5,123},{11,5,161},{1,6,106},{2,6,116},{4,6,100},{5,6,218},{6,6,249},{7,6,203},{8,6,85},{9,6,182},{10,6,26},{11,6,235},{1,7,198},{2,7,53},{3,7,82},{4,7,198},{5,7,51},{6,7,8},{7,7,42},{8,7,217},{9,7,136},{10,7,9},{11,7,245},{1,8,236},{2,8,17},{3,8,223},{4,8,31},{7,8,6},{8,8,220},{9,8,55},{10,8,52},{11,8,181},{1,9,249},{2,9,8},{3,9,234},{4,9,68},{5,9,7},{6,9,51},{7,9,152},{8,9,254},{9,9,28},{10,9,174},{11,9,43},{1,10,213},{2,10,34},{3,10,82},{4,10,222},{5,10,245},{6,10,186},{7,10,35},{8,10,148},{9,10,243},{10,10,93},{1,11,123},{2,11,131},{1,12,7},{2,12,192},{3,12,135},{4,12,38},{5,12,11},{6,12,7},{7,12,34},{8,12,94},{9,12,167},{10,12,3},{2,13,6},{3,13,117},{4,13,208},{5,13,246},{6,13,247},{7,13,216},{8,13,149},{9,13,49}};
      g.pixels.assign (p, p+93);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,3,97},{4,3,255},{5,3,104},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+41);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,253},{5,3,232},{6,3,171},{7,3,34},{1,4,255},{4,4,1},{5,4,23},{6,4,129},{7,4,203},{1,5,255},{6,5,13},{7,5,245},{1,6,255},{4,6,2},{5,6,26},{6,6,141},{7,6,171},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,231},{7,7,36},{1,8,255},{4,8,2},{5,8,29},{6,8,140},{7,8,195},{1,9,255},{6,9,16},{7,9,248},{1,10,255},{4,10,1},{5,10,20},{6,10,137},{7,10,195},{1,11,255},{2,11,255},{3,11,255},{4,11,254},{5,11,235},{6,11,167},{7,11,24}};
      g.pixels.assign (p, p+47);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{2,3,72},{3,3,205},{4,3,248},{5,3,234},{6,3,160},{7,3,21},{1,4,61},{2,4,238},{3,4,84},{4,4,7},{5,4,29},{6,4,146},{7,4,156},{1,5,179},{2,5,109},{1,6,225},{2,6,29},{1,7,247},{2,7,7},{1,8,222},{2,8,34},{1,9,165},{2,9,120},{1,10,46},{2,10,241},{3,10,92},{4,10,8},{5,10,21},{6,10,119},{7,10,172},{2,11,71},{3,11,206},{4,11,248},{5,11,233},{6,11,160},{7,11,22}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,236},{5,3,174},{6,3,46},{1,4,255},{3,4,2},{4,4,33},{5,4,128},{6,4,239},{7,4,44},{1,5,255},{6,5,126},{7,5,171},{1,6,255},{6,6,25},{7,6,235},{1,7,255},{6,7,8},{7,7,250},{1,8,255},{6,8,42},{7,8,228},{1,9,255},{6,9,139},{7,9,159},{1,10,255},{4,10,27},{5,10,123},{6,10,237},{7,10,36},{1,11,255},{2,11,255},{3,11,255},{4,11,244},{5,11,182},{6,11,45}};
      g.pixels.assign (p, p+38);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{7,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,255},{7,11,255}};
      g.pixels.assign (p, p+26);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+18);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{2,3,73},{3,3,204},{4,3,248},{5,3,236},{6,3,172},{7,3,29},{1,4,61},{2,4,235},{3,4,80},{4,4,7},{5,4,24},{6,4,131},{7,4,177},{1,5,179},{2,5,103},{1,6,225},{2,6,26},{1,7,250},{2,7,7},{5,7,255},{6,7,255},{7,7,255},{8,7,255},{1,8,230},{2,8,38},{8,8,255},{1,9,162},{2,9,136},{7,9,2},{8,9,255},{1,10,35},{2,10,235},{3,10,124},{4,10,20},{5,10,8},{6,10,56},{7,10,182},{8,10,119},{2,11,40},{3,11,174},{4,11,238},{5,11,248},{6,11,213},{7,11,110}};
      g.pixels.assign (p, p+44);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{7,3,255},{1,4,255},{7,4,255},{1,5,255},{7,5,255},{1,6,255},{7,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{7,7,255},{1,8,255},{7,8,255},{1,9,255},{7,9,255},{1,10,255},{7,10,255},{1,11,255},{7,11,255}};
      g.pixels.assign (p, p+23);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+9);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 6;
    {
      const ClientSide::Pixel p[] = {{2,3,255},{3,3,255},{4,3,255},{4,4,255},{4,5,255},{4,6,255},{4,7,255},{4,8,255},{3,9,9},{4,9,244},{0,10,198},{1,10,125},{2,10,8},{3,10,110},{4,10,186},{0,11,59},{1,11,209},{2,11,246},{3,11,198},{4,11,37}};
      g.pixels.assign (p, p+20);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{6,3,32},{7,3,213},{8,3,107},{1,4,255},{5,4,48},{6,4,215},{7,4,70},{1,5,255},{4,5,66},{5,5,207},{6,5,41},{1,6,255},{3,6,87},{4,6,190},{5,6,20},{1,7,255},{2,7,111},{3,7,216},{4,7,215},{5,7,21},{1,8,255},{2,8,113},{3,8,1},{4,8,127},{5,8,208},{6,8,17},{1,9,255},{5,9,111},{6,9,201},{7,9,14},{1,10,255},{6,10,97},{7,10,192},{8,10,11},{1,11,255},{7,11,83},{8,11,181},{9,11,8}};
      g.pixels.assign (p, p+39);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255}};
      g.pixels.assign (p, p+13);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 11;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,71},{8,3,59},{9,3,255},{1,4,250},{2,4,179},{3,4,1},{8,4,171},{9,4,248},{1,5,255},{2,5,146},{3,5,84},{7,5,73},{8,5,148},{9,5,255},{1,6,255},{2,6,40},{3,6,213},{4,6,3},{6,6,1},{7,6,204},{8,6,42},{9,6,255},{1,7,255},{3,7,179},{4,7,97},{6,7,86},{7,7,181},{9,7,255},{1,8,255},{3,8,64},{4,8,224},{5,8,9},{6,8,216},{7,8,65},{9,8,255},{1,9,255},{4,9,204},{5,9,149},{6,9,204},{9,9,255},{1,10,255},{4,10,89},{5,10,254},{6,10,89},{9,10,255},{1,11,255},{4,11,4},{5,11,195},{6,11,4},{9,11,255}};
      g.pixels.assign (p, p+51);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,250},{2,3,48},{7,3,255},{1,4,248},{2,4,179},{3,4,6},{7,4,255},{1,5,252},{2,5,67},{3,5,133},{7,5,255},{1,6,255},{3,6,146},{4,6,56},{7,6,255},{1,7,255},{3,7,11},{4,7,182},{5,7,9},{7,7,255},{1,8,255},{4,8,61},{5,8,142},{7,8,255},{1,9,255},{5,9,139},{6,9,64},{7,9,251},{1,10,255},{5,10,8},{6,10,183},{7,10,246},{1,11,255},{6,11,54},{7,11,252}};
      g.pixels.assign (p, p+35);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{2,3,56},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,48},{2,4,239},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,172},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,252},{2,7,8},{7,7,8},{8,7,251},{1,8,232},{2,8,35},{7,8,32},{8,8,229},{1,9,167},{2,9,127},{7,9,121},{8,9,162},{1,10,43},{2,10,239},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{2,11,53},{3,11,189},{4,11,244},{5,11,244},{6,11,185},{7,11,48}};
      g.pixels.assign (p, p+48);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,232},{6,3,168},{7,3,28},{1,4,255},{4,4,1},{5,4,22},{6,4,135},{7,4,191},{1,5,255},{6,5,14},{7,5,242},{1,6,255},{5,6,21},{6,6,135},{7,6,182},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,231},{6,7,161},{7,7,22},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+30);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{2,3,55},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,47},{2,4,238},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,171},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,249},{2,7,8},{7,7,8},{8,7,249},{1,8,224},{2,8,35},{7,8,32},{8,8,221},{1,9,164},{2,9,127},{7,9,121},{8,9,156},{1,10,42},{2,10,237},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,229},{8,10,35},{2,11,47},{3,11,194},{4,11,248},{5,11,249},{6,11,182},{7,11,43},{4,12,38},{5,12,243},{6,12,58},{7,12,6},{5,13,106},{6,13,235},{7,13,240}};
      g.pixels.assign (p, p+55);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,238},{6,3,178},{7,3,33},{1,4,255},{5,4,18},{6,4,128},{7,4,195},{1,5,255},{6,5,13},{7,5,244},{1,6,255},{5,6,16},{6,6,135},{7,6,187},{1,7,255},{2,7,255},{3,7,255},{4,7,253},{5,7,206},{6,7,160},{7,7,23},{1,8,255},{4,8,146},{5,8,91},{1,9,255},{4,9,7},{5,9,196},{6,9,40},{1,10,255},{5,10,40},{6,10,200},{7,10,9},{1,11,255},{6,11,97},{7,11,159}};
      g.pixels.assign (p, p+39);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,27},{2,3,164},{3,3,230},{4,3,251},{5,3,240},{6,3,183},{7,3,49},{1,4,203},{2,4,125},{3,4,26},{4,4,4},{5,4,18},{6,4,94},{7,4,148},{1,5,245},{2,5,10},{1,6,155},{2,6,86},{3,6,89},{4,6,42},{5,6,1},{1,7,2},{2,7,90},{3,7,174},{4,7,233},{5,7,235},{6,7,150},{7,7,19},{5,8,35},{6,8,127},{7,8,189},{6,9,17},{7,9,243},{1,10,188},{2,10,122},{3,10,23},{4,10,4},{5,10,24},{6,10,141},{7,10,182},{1,11,31},{2,11,169},{3,11,233},{4,11,251},{5,11,235},{6,11,156},{7,11,22}};
      g.pixels.assign (p, p+47);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,3,255},{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{3,4,255},{3,5,255},{3,6,255},{3,7,255},{3,8,255},{3,9,255},{3,10,255},{3,11,255}};
      g.pixels.assign (p, p+15);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{7,3,255},{1,4,255},{7,4,255},{1,5,255},{7,5,255},{1,6,255},{7,6,255},{1,7,255},{7,7,255},{1,8,254},{2,8,4},{6,8,6},{7,8,253},{1,9,227},{2,9,44},{6,9,55},{7,9,219},{1,10,91},{2,10,196},{3,10,42},{4,10,8},{5,10,55},{6,10,210},{7,10,84},{2,11,70},{3,11,208},{4,11,247},{5,11,215},{6,11,102}};
      g.pixels.assign (p, p+30);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{0,3,201},{1,3,114},{7,3,114},{8,3,200},{0,4,92},{1,4,206},{7,4,207},{8,4,90},{0,5,6},{1,5,231},{2,5,41},{6,5,42},{7,5,230},{8,5,5},{1,6,128},{2,6,134},{6,6,135},{7,6,126},{1,7,24},{2,7,218},{3,7,1},{5,7,1},{6,7,218},{7,7,22},{2,8,165},{3,8,62},{5,8,63},{6,8,161},{2,9,55},{3,9,190},{4,9,11},{5,9,190},{6,9,51},{3,10,201},{4,10,175},{5,10,197},{3,11,91},{4,11,255},{5,11,87}};
      g.pixels.assign (p, p+39);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 13;
    {
      const ClientSide::Pixel p[] = {{0,3,210},{1,3,108},{5,3,17},{6,3,247},{7,3,34},{11,3,104},{12,3,209},{0,4,118},{1,4,188},{5,4,108},{6,4,254},{7,4,126},{11,4,184},{12,4,117},{0,5,28},{1,5,249},{2,5,17},{5,5,206},{6,5,165},{7,5,218},{10,5,14},{11,5,246},{12,5,27},{1,6,190},{2,6,93},{4,6,48},{5,6,216},{6,6,18},{7,6,214},{8,6,56},{10,6,88},{11,6,188},{1,7,98},{2,7,174},{4,7,147},{5,7,103},{7,7,105},{8,7,149},{10,7,168},{11,7,95},{1,8,14},{2,8,236},{3,8,12},{4,8,227},{5,8,15},{7,8,16},{8,8,227},{9,8,8},{10,8,231},{11,8,13},{2,9,170},{3,9,129},{4,9,164},{8,9,167},{9,9,126},{10,9,166},{2,10,78},{3,10,243},{4,10,66},{8,10,70},{9,10,243},{10,10,74},{2,11,5},{3,11,203},{4,11,1},{8,11,2},{9,11,204},{10,11,4}};
      g.pixels.assign (p, p+68);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,3,170},{1,3,101},{5,3,99},{6,3,163},{0,4,21},{1,4,214},{2,4,19},{4,4,16},{5,4,199},{6,4,13},{1,5,83},{2,5,154},{4,5,140},{5,5,55},{2,6,165},{3,6,99},{4,6,118},{2,7,69},{3,7,253},{4,7,35},{1,8,3},{2,8,206},{3,8,120},{4,8,194},{5,8,3},{1,9,114},{2,9,162},{4,9,153},{5,9,124},{0,10,29},{1,10,225},{2,10,21},{4,10,18},{5,10,230},{6,10,53},{0,11,176},{1,11,99},{5,11,98},{6,11,218},{7,11,11}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,175},{2,3,68},{6,3,68},{7,3,174},{1,4,28},{2,4,202},{3,4,5},{5,4,5},{6,4,200},{7,4,27},{2,5,109},{3,5,119},{5,5,115},{6,5,108},{2,6,2},{3,6,190},{4,6,52},{5,6,187},{6,6,2},{3,7,47},{4,7,246},{5,7,47},{4,8,255},{4,9,255},{4,10,255},{4,11,255}};
      g.pixels.assign (p, p+26);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,136},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{4,4,102},{5,4,170},{3,5,11},{4,5,226},{5,5,36},{3,6,127},{4,6,148},{2,7,23},{3,7,230},{4,7,22},{2,8,151},{3,8,126},{1,9,39},{2,9,227},{3,9,11},{1,10,175},{2,10,105},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,76}};
      g.pixels.assign (p, p+28);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{1,12,255},{1,13,255},{1,14,255},{2,14,255},{3,14,255}};
      g.pixels.assign (p, p+16);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{-1,3,21},{0,3,218},{1,3,2},{0,4,174},{1,4,71},{0,5,77},{1,5,173},{0,6,4},{1,6,226},{2,6,23},{1,7,138},{2,7,119},{1,8,41},{2,8,219},{3,8,1},{2,9,199},{3,9,66},{2,10,102},{3,10,167},{2,11,14},{3,11,239},{4,11,20}};
      g.pixels.assign (p, p+22);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,3,255},{1,3,255},{2,3,255},{2,4,255},{2,5,255},{2,6,255},{2,7,255},{2,8,255},{2,9,255},{2,10,255},{2,11,255},{2,12,255},{2,13,255},{0,14,255},{1,14,255},{2,14,255}};
      g.pixels.assign (p, p+16);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{1,3,80},{2,3,243},{3,3,87},{1,4,175},{2,4,91},{3,4,180},{0,5,21},{1,5,199},{3,5,201},{4,5,23},{0,6,112},{1,6,108},{3,6,109},{4,6,114},{0,7,201},{1,7,18},{3,7,19},{4,7,201}};
      g.pixels.assign (p, p+18);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,14,255},{1,14,255},{2,14,255},{3,14,255},{4,14,255},{5,14,255},{6,14,255}};
      g.pixels.assign (p, p+7);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,2,183},{2,2,62},{1,3,13},{2,3,155}};
      g.pixels.assign (p, p+4);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+34);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{2,5,82},{3,5,239},{4,5,214},{5,5,44},{1,6,254},{2,6,132},{3,6,14},{4,6,125},{5,6,180},{1,7,255},{2,7,26},{4,7,22},{5,7,234},{1,8,255},{2,8,5},{4,8,5},{5,8,249},{1,9,255},{2,9,23},{4,9,24},{5,9,232},{1,10,253},{2,10,125},{3,10,12},{4,10,124},{5,10,175},{0,11,4},{1,11,243},{2,11,81},{3,11,239},{4,11,211},{5,11,40}};
      g.pixels.assign (p, p+35);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,20},{2,5,181},{3,5,246},{4,5,208},{5,5,53},{1,6,160},{2,6,135},{3,6,12},{4,6,81},{5,6,205},{1,7,233},{2,7,22},{1,8,250},{2,8,4},{1,9,230},{2,9,28},{1,10,156},{2,10,146},{3,10,16},{4,10,86},{5,10,212},{1,11,19},{2,11,184},{3,11,248},{4,11,211},{5,11,56}};
      g.pixels.assign (p, p+26);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{5,3,255},{5,4,255},{1,5,40},{2,5,210},{3,5,235},{4,5,76},{5,5,255},{1,6,176},{2,6,127},{3,6,13},{4,6,126},{5,6,255},{1,7,234},{2,7,21},{4,7,26},{5,7,255},{1,8,250},{2,8,4},{4,8,6},{5,8,255},{1,9,235},{2,9,21},{4,9,29},{5,9,255},{1,10,182},{2,10,122},{3,10,12},{4,10,128},{5,10,254},{1,11,46},{2,11,214},{3,11,236},{4,11,78},{5,11,246},{6,11,1}};
      g.pixels.assign (p, p+35);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,25},{2,5,186},{3,5,245},{4,5,191},{5,5,16},{1,6,169},{2,6,141},{3,6,12},{4,6,136},{5,6,149},{1,7,236},{2,7,24},{4,7,22},{5,7,223},{1,8,250},{2,8,255},{3,8,255},{4,8,255},{5,8,248},{1,9,227},{2,9,13},{1,10,155},{2,10,129},{3,10,10},{4,10,99},{5,10,189},{1,11,21},{2,11,179},{3,11,243},{4,11,213},{5,11,61}};
      g.pixels.assign (p, p+31);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,3,4},{1,3,135},{2,3,246},{0,4,33},{1,4,255},{2,4,17},{0,5,248},{1,5,255},{2,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+15);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,38},{2,5,216},{3,5,239},{4,5,94},{5,5,246},{6,5,1},{1,6,178},{2,6,129},{3,6,15},{4,6,150},{5,6,254},{1,7,236},{2,7,23},{4,7,38},{5,7,255},{1,8,251},{2,8,4},{4,8,8},{5,8,255},{1,9,238},{2,9,19},{4,9,36},{5,9,255},{1,10,187},{2,10,120},{3,10,14},{4,10,147},{5,10,255},{1,11,52},{2,11,220},{3,11,234},{4,11,86},{5,11,255},{4,12,22},{5,12,239},{1,13,174},{2,13,78},{3,13,13},{4,13,140},{5,13,184},{1,14,74},{2,14,220},{3,14,246},{4,14,196},{5,14,38}};
      g.pixels.assign (p, p+45);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,253},{2,5,92},{3,5,236},{4,5,231},{5,5,89},{1,6,251},{2,6,138},{3,6,14},{4,6,84},{5,6,224},{1,7,255},{2,7,19},{4,7,4},{5,7,253},{1,8,255},{5,8,255},{1,9,255},{5,9,255},{1,10,255},{5,10,255},{1,11,255},{5,11,255}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+8);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{1,12,255},{0,13,31},{1,13,246},{0,14,249},{1,14,121}};
      g.pixels.assign (p, p+13);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{4,5,6},{5,5,164},{6,5,121},{1,6,255},{3,6,17},{4,6,191},{5,6,109},{1,7,255},{2,7,33},{3,7,212},{4,7,97},{1,8,255},{2,8,229},{3,8,241},{4,8,67},{1,9,255},{2,9,51},{3,9,68},{4,9,231},{5,9,41},{1,10,255},{4,10,75},{5,10,217},{6,10,22},{1,11,255},{5,11,83},{6,11,196},{7,11,9}};
      g.pixels.assign (p, p+31);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+9);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 11;
    {
      const ClientSide::Pixel p[] = {{0,5,7},{1,5,239},{2,5,90},{3,5,232},{4,5,236},{5,5,91},{6,5,99},{7,5,233},{8,5,235},{9,5,90},{0,6,1},{1,6,251},{2,6,137},{3,6,13},{4,6,85},{5,6,237},{6,6,141},{7,6,13},{8,6,85},{9,6,223},{1,7,255},{2,7,20},{4,7,6},{5,7,255},{6,7,19},{8,7,6},{9,7,253},{1,8,255},{5,8,255},{9,8,255},{1,9,255},{5,9,255},{9,9,255},{1,10,255},{5,10,255},{9,10,255},{1,11,255},{5,11,255},{9,11,255}};
      g.pixels.assign (p, p+39);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,5,7},{1,5,236},{2,5,93},{3,5,233},{4,5,234},{5,5,90},{0,6,1},{1,6,250},{2,6,139},{3,6,13},{4,6,85},{5,6,223},{1,7,255},{2,7,20},{4,7,6},{5,7,253},{1,8,255},{5,8,255},{1,9,255},{5,9,255},{1,10,255},{5,10,255},{1,11,255},{5,11,255}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,27},{2,5,191},{3,5,243},{4,5,194},{5,5,31},{1,6,164},{2,6,136},{3,6,13},{4,6,131},{5,6,167},{1,7,230},{2,7,23},{4,7,22},{5,7,229},{1,8,249},{2,8,3},{4,8,3},{5,8,249},{1,9,228},{2,9,22},{4,9,22},{5,9,228},{1,10,160},{2,10,132},{3,10,11},{4,10,137},{5,10,161},{1,11,26},{2,11,192},{3,11,243},{4,11,188},{5,11,26}};
      g.pixels.assign (p, p+32);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,5,4},{1,5,243},{2,5,86},{3,5,241},{4,5,221},{5,5,44},{1,6,253},{2,6,132},{3,6,13},{4,6,127},{5,6,181},{1,7,255},{2,7,24},{4,7,21},{5,7,236},{1,8,255},{2,8,3},{4,8,4},{5,8,250},{1,9,255},{2,9,23},{4,9,21},{5,9,235},{1,10,254},{2,10,126},{3,10,13},{4,10,127},{5,10,177},{1,11,254},{2,11,83},{3,11,239},{4,11,219},{5,11,39},{1,12,255},{1,13,255},{1,14,255}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,41},{2,5,211},{3,5,239},{4,5,83},{5,5,247},{6,5,5},{1,6,177},{2,6,128},{3,6,13},{4,6,132},{5,6,255},{1,7,234},{2,7,22},{4,7,26},{5,7,255},{1,8,250},{2,8,3},{4,8,4},{5,8,255},{1,9,235},{2,9,18},{4,9,26},{5,9,255},{1,10,182},{2,10,121},{3,10,12},{4,10,135},{5,10,255},{1,11,46},{2,11,214},{3,11,240},{4,11,86},{5,11,255},{5,12,255},{5,13,255},{5,14,255}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,5,8},{1,5,237},{2,5,125},{3,5,246},{0,6,1},{1,6,249},{2,6,127},{3,6,9},{1,7,255},{2,7,24},{1,8,255},{2,8,1},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+15);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,84},{2,5,219},{3,5,248},{4,5,212},{5,5,63},{1,6,235},{2,6,53},{3,6,5},{4,6,89},{5,6,205},{1,7,212},{2,7,135},{3,7,31},{1,8,33},{2,8,155},{3,8,233},{4,8,207},{5,8,62},{3,9,1},{4,9,92},{5,9,226},{1,10,200},{2,10,86},{3,10,5},{4,10,58},{5,10,229},{1,11,70},{2,11,216},{3,11,249},{4,11,217},{5,11,75}};
      g.pixels.assign (p, p+31);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,3,187},{1,4,225},{0,5,200},{1,5,255},{2,5,255},{3,5,132},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,228},{2,10,36},{3,10,6},{1,11,47},{2,11,219},{3,11,234}};
      g.pixels.assign (p, p+16);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,255},{5,5,255},{1,6,255},{5,6,255},{1,7,255},{5,7,255},{1,8,255},{5,8,255},{1,9,253},{2,9,5},{4,9,23},{5,9,255},{1,10,228},{2,10,82},{3,10,13},{4,10,140},{5,10,251},{1,11,95},{2,11,235},{3,11,235},{4,11,92},{5,11,238},{6,11,1}};
      g.pixels.assign (p, p+23);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{-1,5,27},{0,5,254},{1,5,18},{3,5,15},{4,5,253},{5,5,27},{0,6,206},{1,6,70},{3,6,67},{4,6,206},{0,7,130},{1,7,122},{3,7,119},{4,7,130},{0,8,54},{1,8,175},{3,8,172},{4,8,54},{0,9,1},{1,9,208},{2,9,26},{3,9,206},{4,9,1},{1,10,158},{2,10,185},{3,10,158},{1,11,82},{2,11,255},{3,11,82}};
      g.pixels.assign (p, p+29);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{-1,5,23},{0,5,246},{1,5,13},{3,5,12},{4,5,240},{5,5,12},{7,5,13},{8,5,246},{9,5,23},{0,6,191},{1,6,85},{3,6,87},{4,6,251},{5,6,86},{7,6,86},{8,6,190},{0,7,105},{1,7,164},{3,7,168},{4,7,166},{5,7,167},{7,7,165},{8,7,104},{0,8,22},{1,8,235},{2,8,12},{3,8,237},{4,8,36},{5,8,237},{6,8,11},{7,8,235},{8,8,21},{1,9,188},{2,9,135},{3,9,185},{5,9,189},{6,9,135},{7,9,187},{1,10,102},{2,10,245},{3,10,103},{5,10,107},{6,10,245},{7,10,101},{1,11,20},{2,11,249},{3,11,23},{5,11,26},{6,11,249},{7,11,19}};
      g.pixels.assign (p, p+50);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,5,12},{1,5,226},{2,5,73},{4,5,77},{5,5,226},{6,5,12},{1,6,77},{2,6,210},{3,6,5},{4,6,212},{5,6,75},{2,7,171},{3,7,184},{4,7,168},{2,8,64},{3,8,255},{4,8,64},{2,9,191},{3,9,154},{4,9,192},{1,10,90},{2,10,197},{3,10,1},{4,10,203},{5,10,90},{0,11,13},{1,11,230},{2,11,63},{4,11,73},{5,11,230},{6,11,13}};
      g.pixels.assign (p, p+31);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{-1,5,17},{0,5,221},{1,5,8},{5,5,10},{6,5,220},{7,5,17},{0,6,144},{1,6,102},{5,6,117},{6,6,142},{0,7,31},{1,7,212},{2,7,1},{4,7,11},{5,7,229},{6,7,29},{1,8,166},{2,8,80},{4,8,118},{5,8,161},{1,9,49},{2,9,233},{3,9,20},{4,9,231},{5,9,44},{2,10,188},{3,10,196},{4,10,181},{2,11,71},{3,11,255},{4,11,63},{2,12,86},{3,12,200},{0,13,4},{1,13,61},{2,13,232},{3,13,65},{0,14,249},{1,14,227},{2,14,96}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,192},{2,5,255},{3,5,255},{4,5,255},{5,5,255},{4,6,113},{5,6,151},{3,7,64},{4,7,194},{5,7,6},{2,8,28},{3,8,209},{4,8,26},{1,9,7},{2,9,196},{3,9,60},{1,10,155},{2,10,109},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,60}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,3,66},{2,3,236},{3,3,255},{1,4,223},{2,4,73},{3,4,2},{1,5,255},{0,6,2},{1,6,255},{-1,7,2},{0,7,84},{1,7,228},{-1,8,92},{0,8,250},{1,8,73},{-1,9,3},{0,9,98},{1,9,213},{0,10,5},{1,10,255},{1,11,255},{1,12,255},{2,12,3},{1,13,236},{2,13,87},{3,13,3},{1,14,97},{2,14,234},{3,14,255}};
      g.pixels.assign (p, p+29);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{1,12,255},{1,13,255},{1,14,255}};
      g.pixels.assign (p, p+12);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,3,255},{1,3,236},{2,3,66},{0,4,3},{1,4,76},{2,4,222},{2,5,255},{2,6,255},{3,6,1},{2,7,229},{3,7,84},{4,7,2},{2,8,76},{3,8,249},{4,8,96},{2,9,217},{3,9,98},{4,9,3},{2,10,255},{3,10,5},{2,11,255},{1,12,3},{2,12,255},{0,13,3},{1,13,90},{2,13,236},{0,14,255},{1,14,234},{2,14,97}};
      g.pixels.assign (p, p+29);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{1,6,107},{2,6,208},{3,6,200},{4,6,86},{5,6,10},{6,6,123},{1,7,170},{2,7,30},{3,7,74},{4,7,189},{5,7,246},{6,7,107}};
      g.pixels.assign (p, p+12);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{0,3,255},{1,3,255},{2,3,255},{3,3,255},{0,4,255},{3,4,255},{0,5,255},{3,5,255},{0,6,255},{3,6,255},{0,7,255},{3,7,255},{0,8,255},{3,8,255},{0,9,255},{3,9,255},{0,10,255},{3,10,255},{0,11,255},{1,11,255},{2,11,255},{3,11,255}};
      g.pixels.assign (p, p+22);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 0;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,5,255},{1,7,196},{1,8,206},{1,9,215},{1,10,224},{1,11,233},{1,12,242},{1,13,251}};
      g.pixels.assign (p, p+8);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,3,255},{1,4,36},{2,4,210},{3,4,255},{4,4,206},{5,4,41},{1,5,170},{2,5,103},{3,5,255},{4,5,102},{5,5,195},{1,6,231},{2,6,16},{3,6,255},{4,6,3},{5,6,27},{1,7,250},{2,7,3},{3,7,255},{1,8,234},{2,8,20},{3,8,255},{4,8,3},{5,8,52},{1,9,172},{2,9,111},{3,9,255},{4,9,101},{5,9,199},{1,10,32},{2,10,207},{3,10,255},{4,10,213},{5,10,44},{3,11,255}};
      g.pixels.assign (p, p+35);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,57},{3,3,205},{4,3,246},{5,3,216},{6,3,75},{2,4,214},{3,4,92},{4,4,8},{5,4,81},{6,4,150},{2,5,253},{3,5,2},{2,6,255},{0,7,16},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,104},{2,8,255},{1,9,18},{2,9,232},{6,9,10},{1,10,130},{2,10,119},{5,10,78},{6,10,224},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,235},{6,11,83}};
      g.pixels.assign (p, p+33);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,4,4},{6,4,3},{0,5,5},{1,5,209},{2,5,128},{3,5,219},{4,5,218},{5,5,131},{6,5,202},{7,5,2},{1,6,135},{2,6,183},{3,6,30},{4,6,31},{5,6,185},{6,6,130},{1,7,220},{2,7,29},{5,7,29},{6,7,218},{1,8,221},{2,8,28},{5,8,28},{6,8,219},{1,9,134},{2,9,180},{3,9,28},{4,9,29},{5,9,182},{6,9,128},{0,10,6},{1,10,216},{2,10,132},{3,10,221},{4,10,220},{5,10,136},{6,10,209},{7,10,3},{1,11,7},{6,11,6}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,3,170},{1,3,64},{5,3,65},{6,3,170},{0,4,23},{1,4,209},{2,4,17},{4,4,18},{5,4,210},{6,4,24},{1,5,94},{2,5,172},{4,5,173},{5,5,97},{2,6,183},{3,6,177},{4,6,186},{0,7,108},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,112},{3,8,254},{0,9,108},{1,9,255},{2,9,255},{3,9,255},{4,9,255},{5,9,255},{6,9,112},{3,10,255},{3,11,255}};
      g.pixels.assign (p, p+34);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,10,255},{1,11,255},{1,12,255},{1,13,255},{1,14,255}};
      g.pixels.assign (p, p+10);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,91},{2,3,222},{3,3,248},{4,3,206},{5,3,50},{1,4,239},{2,4,51},{3,4,4},{4,4,88},{5,4,133},{1,5,188},{2,5,125},{3,5,41},{1,6,65},{2,6,215},{3,6,185},{4,6,212},{5,6,69},{1,7,229},{2,7,26},{4,7,56},{5,7,232},{1,8,211},{2,8,105},{3,8,3},{4,8,61},{5,8,211},{1,9,32},{2,9,157},{3,9,224},{4,9,246},{5,9,60},{3,10,3},{4,10,109},{5,10,215},{1,11,183},{2,11,72},{3,11,4},{4,11,61},{5,11,231},{1,12,72},{2,12,219},{3,12,250},{4,12,219},{5,12,76}};
      g.pixels.assign (p, p+45);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,3,255},{2,3,255}};
      g.pixels.assign (p, p+2);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{1,3,6},{2,3,122},{3,3,215},{4,3,248},{5,3,220},{6,3,128},{7,3,9},{0,4,7},{1,4,190},{2,4,119},{3,4,29},{4,4,8},{5,4,41},{6,4,126},{7,4,196},{8,4,9},{0,5,126},{1,5,117},{2,5,48},{3,5,201},{4,5,245},{5,5,213},{6,5,66},{7,5,126},{8,5,128},{0,6,219},{1,6,27},{2,6,204},{3,6,111},{4,6,10},{5,6,37},{6,6,118},{7,6,41},{8,6,220},{0,7,248},{1,7,6},{2,7,246},{3,7,10},{7,7,9},{8,7,248},{0,8,219},{1,8,26},{2,8,201},{3,8,120},{4,8,10},{5,8,37},{6,8,122},{7,8,40},{8,8,216},{0,9,128},{1,9,115},{2,9,42},{3,9,195},{4,9,245},{5,9,218},{6,9,70},{7,9,125},{8,9,124},{0,10,8},{1,10,193},{2,10,115},{3,10,26},{4,10,7},{5,10,39},{6,10,123},{7,10,193},{8,10,7},{1,11,7},{2,11,126},{3,11,219},{4,11,248},{5,11,219},{6,11,128},{7,11,8}};
      g.pixels.assign (p, p+74);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,3,144},{1,3,244},{2,3,93},{0,4,80},{1,4,27},{2,4,245},{0,5,119},{1,5,249},{2,5,255},{0,6,239},{1,6,58},{2,6,255},{3,6,26},{0,7,125},{1,7,192},{2,7,112},{3,7,246}};
      g.pixels.assign (p, p+17);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,6,110},{2,6,183},{4,6,110},{5,6,183},{0,7,76},{1,7,206},{2,7,13},{3,7,76},{4,7,206},{5,7,13},{0,8,235},{1,8,55},{3,8,235},{4,8,55},{0,9,83},{1,9,203},{2,9,12},{3,9,83},{4,9,203},{5,9,12},{1,10,112},{2,10,182},{4,10,112},{5,10,182}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{6,8,255},{6,9,255},{6,10,255}};
      g.pixels.assign (p, p+9);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,8,255},{2,8,255}};
      g.pixels.assign (p, p+2);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{1,3,6},{2,3,122},{3,3,215},{4,3,248},{5,3,220},{6,3,128},{7,3,9},{0,4,7},{1,4,190},{2,4,119},{3,4,29},{4,4,8},{5,4,41},{6,4,126},{7,4,196},{8,4,9},{0,5,126},{1,5,117},{3,5,255},{4,5,255},{5,5,239},{6,5,77},{7,5,126},{8,5,128},{0,6,219},{1,6,27},{3,6,255},{5,6,59},{6,6,230},{7,6,41},{8,6,220},{0,7,248},{1,7,6},{3,7,255},{4,7,255},{5,7,248},{6,7,76},{7,7,9},{8,7,248},{0,8,219},{1,8,26},{3,8,255},{5,8,159},{6,8,61},{7,8,40},{8,8,216},{0,9,128},{1,9,115},{3,9,255},{5,9,19},{6,9,199},{7,9,137},{8,9,124},{0,10,8},{1,10,193},{2,10,115},{3,10,26},{4,10,7},{5,10,39},{6,10,123},{7,10,193},{8,10,7},{1,11,7},{2,11,126},{3,11,219},{4,11,248},{5,11,219},{6,11,128},{7,11,8}};
      g.pixels.assign (p, p+69);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{0,2,255},{1,2,255},{2,2,255},{3,2,255},{4,2,255},{5,2,255},{6,2,255}};
      g.pixels.assign (p, p+7);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{1,3,73},{2,3,227},{3,3,226},{4,3,73},{1,4,227},{2,4,59},{3,4,60},{4,4,227},{1,5,227},{2,5,59},{3,5,60},{4,5,227},{1,6,73},{2,6,227},{3,6,226},{4,6,73}};
      g.pixels.assign (p, p+16);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,4,128},{3,5,255},{3,6,255},{0,7,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{3,8,255},{3,9,255},{3,10,128},{0,11,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,255}};
      g.pixels.assign (p, p+20);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,3,97},{1,3,230},{2,3,243},{3,3,101},{0,4,94},{1,4,34},{2,4,43},{3,4,229},{1,5,64},{2,5,182},{3,5,55},{0,6,96},{1,6,137},{2,6,7},{0,7,255},{1,7,255},{2,7,255},{3,7,255},{4,7,20}};
      g.pixels.assign (p, p+19);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{0,3,118},{1,3,237},{2,3,226},{3,3,106},{0,4,90},{1,4,31},{2,4,68},{3,4,231},{1,5,255},{2,5,242},{3,5,87},{0,6,86},{1,6,28},{2,6,64},{3,6,237},{0,7,127},{1,7,238},{2,7,200},{3,7,85}};
      g.pixels.assign (p, p+19);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,2,13},{2,2,208},{3,2,105},{1,3,164},{2,3,74}};
      g.pixels.assign (p, p+5);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,255},{5,5,255},{1,6,255},{5,6,255},{1,7,255},{5,7,255},{1,8,255},{5,8,255},{1,9,255},{2,9,7},{4,9,20},{5,9,255},{1,10,255},{2,10,96},{3,10,12},{4,10,136},{5,10,245},{6,10,23},{1,11,255},{2,11,204},{3,11,241},{4,11,102},{5,11,117},{6,11,245},{1,12,255},{1,13,255},{1,14,255}};
      g.pixels.assign (p, p+27);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,54},{2,3,212},{3,3,253},{4,3,255},{5,3,255},{6,3,212},{1,4,205},{2,4,255},{3,4,255},{5,4,255},{1,5,247},{2,5,255},{3,5,255},{5,5,255},{1,6,215},{2,6,255},{3,6,255},{5,6,255},{1,7,70},{2,7,228},{3,7,255},{5,7,255},{3,8,255},{5,8,255},{3,9,255},{5,9,255},{3,10,255},{5,10,255},{3,11,255},{5,11,255},{3,12,255},{5,12,255},{3,13,255},{5,13,255}};
      g.pixels.assign (p, p+34);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,8,255}};
      g.pixels.assign (p, p+1);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,12,203},{2,12,108},{1,13,38},{2,13,228},{1,14,249},{2,14,115}};
      g.pixels.assign (p, p+6);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 4;
    {
      const ClientSide::Pixel p[] = {{1,3,90},{2,3,255},{1,4,96},{2,4,255},{2,5,255},{2,6,255},{1,7,255},{2,7,255},{3,7,255}};
      g.pixels.assign (p, p+9);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 5;
    {
      const ClientSide::Pixel p[] = {{0,3,74},{1,3,229},{2,3,231},{3,3,80},{0,4,216},{1,4,62},{2,4,61},{3,4,219},{0,5,248},{1,5,5},{2,5,6},{3,5,248},{0,6,218},{1,6,61},{2,6,65},{3,6,215},{0,7,78},{1,7,231},{2,7,230},{3,7,75}};
      g.pixels.assign (p, p+20);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,6,183},{2,6,109},{4,6,183},{5,6,106},{1,7,13},{2,7,207},{3,7,75},{4,7,13},{5,7,206},{6,7,75},{2,8,55},{3,8,235},{5,8,55},{6,8,235},{1,9,12},{2,9,204},{3,9,83},{4,9,12},{5,9,204},{6,9,82},{1,10,182},{2,10,115},{4,10,182},{5,10,112}};
      g.pixels.assign (p, p+24);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 11;
    {
      const ClientSide::Pixel p[] = {{1,3,90},{2,3,255},{7,3,17},{8,3,205},{9,3,13},{1,4,96},{2,4,255},{7,4,154},{8,4,82},{2,5,255},{6,5,58},{7,5,178},{2,6,255},{5,6,4},{6,6,200},{7,6,32},{1,7,255},{2,7,255},{3,7,255},{5,7,118},{6,7,119},{7,7,110},{8,7,255},{4,8,31},{5,8,201},{6,8,33},{7,8,157},{8,8,255},{4,9,177},{5,9,60},{6,9,165},{7,9,31},{8,9,255},{3,10,82},{4,10,157},{6,10,255},{7,10,255},{8,10,255},{9,10,180},{2,11,12},{3,11,207},{4,11,19},{8,11,255}};
      g.pixels.assign (p, p+43);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 11;
    {
      const ClientSide::Pixel p[] = {{1,3,90},{2,3,255},{7,3,17},{8,3,205},{9,3,13},{1,4,96},{2,4,255},{7,4,154},{8,4,82},{2,5,255},{6,5,58},{7,5,178},{2,6,255},{5,6,4},{6,6,200},{7,6,32},{1,7,255},{2,7,255},{3,7,255},{5,7,118},{6,7,217},{7,7,230},{8,7,243},{9,7,101},{4,8,31},{5,8,201},{6,8,99},{7,8,34},{8,8,43},{9,8,229},{4,9,177},{5,9,60},{7,9,64},{8,9,182},{9,9,55},{3,10,82},{4,10,157},{6,10,96},{7,10,137},{8,10,7},{2,11,12},{3,11,207},{4,11,19},{6,11,255},{7,11,255},{8,11,255},{9,11,255},{10,11,20}};
      g.pixels.assign (p, p+48);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 11;
    {
      const ClientSide::Pixel p[] = {{0,3,118},{1,3,237},{2,3,226},{3,3,106},{7,3,17},{8,3,205},{9,3,13},{0,4,90},{1,4,31},{2,4,68},{3,4,231},{7,4,154},{8,4,82},{1,5,255},{2,5,242},{3,5,87},{6,5,58},{7,5,178},{0,6,86},{1,6,28},{2,6,64},{3,6,237},{5,6,4},{6,6,200},{7,6,32},{0,7,127},{1,7,238},{2,7,200},{3,7,85},{5,7,118},{6,7,119},{7,7,110},{8,7,255},{4,8,31},{5,8,201},{6,8,33},{7,8,157},{8,8,255},{4,9,177},{5,9,60},{6,9,165},{7,9,31},{8,9,255},{3,10,82},{4,10,157},{6,10,255},{7,10,255},{8,10,255},{9,10,180},{2,11,12},{3,11,207},{4,11,19},{8,11,255}};
      g.pixels.assign (p, p+53);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{4,5,255},{3,7,17},{4,7,237},{3,8,117},{4,8,149},{2,9,110},{3,9,195},{4,9,8},{2,10,225},{3,10,40},{2,11,247},{3,11,8},{6,11,4},{2,12,197},{3,12,112},{4,12,8},{5,12,90},{6,12,214},{2,13,43},{3,13,201},{4,13,246},{5,13,204},{6,13,53}};
      g.pixels.assign (p, p+23);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{4,0,193},{5,0,63},{4,1,20},{5,1,171},{3,3,97},{4,3,255},{5,3,104},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+45);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{4,0,64},{5,0,192},{4,1,171},{5,1,20},{3,3,97},{4,3,255},{5,3,104},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+45);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,0,58},{4,0,255},{5,0,66},{3,1,169},{4,1,60},{5,1,169},{3,3,97},{4,3,255},{5,3,104},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+47);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,0,107},{4,0,215},{5,0,50},{6,0,153},{3,1,152},{4,1,49},{5,1,214},{6,1,107},{3,3,97},{4,3,255},{5,3,104},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+49);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,1,255},{6,1,255},{3,3,97},{4,3,255},{5,3,104},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+43);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,1,100},{4,1,242},{5,1,98},{3,2,235},{4,2,59},{5,2,233},{3,3,198},{4,3,255},{5,3,204},{3,4,202},{4,4,143},{5,4,207},{2,5,51},{3,5,216},{4,5,3},{5,5,215},{6,5,56},{2,6,157},{3,6,103},{5,6,101},{6,6,161},{1,7,16},{2,7,225},{3,7,9},{5,7,8},{6,7,224},{7,7,18},{1,8,111},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{7,8,114},{1,9,215},{2,9,41},{6,9,38},{7,9,216},{8,9,1},{0,10,66},{1,10,203},{7,10,200},{8,10,67},{0,11,171},{1,11,110},{7,11,107},{8,11,171}};
      g.pixels.assign (p, p+47);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 13;
    {
      const ClientSide::Pixel p[] = {{3,3,5},{4,3,224},{5,3,240},{6,3,255},{7,3,255},{8,3,255},{9,3,255},{10,3,255},{11,3,255},{3,4,98},{4,4,192},{6,4,255},{2,5,2},{3,5,218},{4,5,64},{6,5,255},{2,6,89},{3,6,189},{6,6,255},{1,7,1},{2,7,210},{3,7,57},{6,7,255},{7,7,255},{8,7,255},{9,7,255},{10,7,255},{1,8,80},{2,8,255},{3,8,255},{4,8,255},{5,8,255},{6,8,255},{1,9,203},{2,9,78},{6,9,255},{0,10,71},{1,10,224},{2,10,2},{6,10,255},{0,11,194},{1,11,121},{6,11,255},{7,11,255},{8,11,255},{9,11,255},{10,11,255},{11,11,255}};
      g.pixels.assign (p, p+48);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{2,3,72},{3,3,205},{4,3,248},{5,3,234},{6,3,160},{7,3,21},{1,4,61},{2,4,238},{3,4,84},{4,4,7},{5,4,29},{6,4,146},{7,4,156},{1,5,179},{2,5,109},{1,6,225},{2,6,29},{1,7,247},{2,7,7},{1,8,222},{2,8,34},{1,9,165},{2,9,120},{1,10,46},{2,10,241},{3,10,92},{4,10,8},{5,10,21},{6,10,119},{7,10,172},{2,11,71},{3,11,206},{4,11,248},{5,11,233},{6,11,160},{7,11,22},{4,12,203},{5,12,108},{4,13,38},{5,13,228},{4,14,249},{5,14,115}};
      g.pixels.assign (p, p+42);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,0,193},{4,0,63},{3,1,20},{4,1,171},{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{7,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,255},{7,11,255}};
      g.pixels.assign (p, p+30);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{4,0,64},{5,0,192},{4,1,171},{5,1,20},{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{7,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,255},{7,11,255}};
      g.pixels.assign (p, p+30);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,0,58},{4,0,255},{5,0,66},{3,1,169},{4,1,60},{5,1,169},{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{7,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,255},{7,11,255}};
      g.pixels.assign (p, p+32);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,1,255},{6,1,255},{1,3,255},{2,3,255},{3,3,255},{4,3,255},{5,3,255},{6,3,255},{7,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255},{2,11,255},{3,11,255},{4,11,255},{5,11,255},{6,11,255},{7,11,255}};
      g.pixels.assign (p, p+28);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,0,193},{1,0,63},{0,1,20},{1,1,171},{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+13);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,0,64},{2,0,192},{1,1,171},{2,1,20},{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+13);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,0,58},{1,0,255},{2,0,66},{0,1,169},{1,1,60},{2,1,169},{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+15);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,1,255},{3,1,255},{1,3,255},{1,4,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+11);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{2,3,255},{3,3,255},{4,3,236},{5,3,174},{6,3,46},{1,4,255},{3,4,2},{4,4,33},{5,4,128},{6,4,239},{7,4,44},{1,5,255},{6,5,126},{7,5,171},{1,6,255},{6,6,25},{7,6,235},{0,7,255},{1,7,255},{2,7,255},{3,7,255},{6,7,8},{7,7,250},{1,8,255},{6,8,42},{7,8,228},{1,9,255},{6,9,139},{7,9,159},{1,10,255},{4,10,27},{5,10,123},{6,10,237},{7,10,36},{1,11,255},{2,11,255},{3,11,255},{4,11,244},{5,11,182},{6,11,45}};
      g.pixels.assign (p, p+41);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,0,107},{4,0,215},{5,0,50},{6,0,153},{3,1,152},{4,1,49},{5,1,214},{6,1,107},{1,3,250},{2,3,48},{7,3,255},{1,4,248},{2,4,179},{3,4,6},{7,4,255},{1,5,252},{2,5,67},{3,5,133},{7,5,255},{1,6,255},{3,6,146},{4,6,56},{7,6,255},{1,7,255},{3,7,11},{4,7,182},{5,7,9},{7,7,255},{1,8,255},{4,8,61},{5,8,142},{7,8,255},{1,9,255},{5,9,139},{6,9,64},{7,9,251},{1,10,255},{5,10,8},{6,10,183},{7,10,246},{1,11,255},{6,11,54},{7,11,252}};
      g.pixels.assign (p, p+43);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{4,0,193},{5,0,63},{4,1,20},{5,1,171},{2,3,56},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,48},{2,4,239},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,172},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,252},{2,7,8},{7,7,8},{8,7,251},{1,8,232},{2,8,35},{7,8,32},{8,8,229},{1,9,167},{2,9,127},{7,9,121},{8,9,162},{1,10,43},{2,10,239},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{2,11,53},{3,11,189},{4,11,244},{5,11,244},{6,11,185},{7,11,48}};
      g.pixels.assign (p, p+52);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{4,0,64},{5,0,192},{4,1,171},{5,1,20},{2,3,56},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,48},{2,4,239},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,172},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,252},{2,7,8},{7,7,8},{8,7,251},{1,8,232},{2,8,35},{7,8,32},{8,8,229},{1,9,167},{2,9,127},{7,9,121},{8,9,162},{1,10,43},{2,10,239},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{2,11,53},{3,11,189},{4,11,244},{5,11,244},{6,11,185},{7,11,48}};
      g.pixels.assign (p, p+52);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{4,0,58},{5,0,255},{6,0,66},{4,1,169},{5,1,60},{6,1,169},{2,3,56},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,48},{2,4,239},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,172},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,252},{2,7,8},{7,7,8},{8,7,251},{1,8,232},{2,8,35},{7,8,32},{8,8,229},{1,9,167},{2,9,127},{7,9,121},{8,9,162},{1,10,43},{2,10,239},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{2,11,53},{3,11,189},{4,11,244},{5,11,244},{6,11,185},{7,11,48}};
      g.pixels.assign (p, p+54);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{3,0,107},{4,0,215},{5,0,50},{6,0,153},{3,1,152},{4,1,49},{5,1,214},{6,1,107},{2,3,56},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,48},{2,4,239},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,172},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,252},{2,7,8},{7,7,8},{8,7,251},{1,8,232},{2,8,35},{7,8,32},{8,8,229},{1,9,167},{2,9,127},{7,9,121},{8,9,162},{1,10,43},{2,10,239},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{2,11,53},{3,11,189},{4,11,244},{5,11,244},{6,11,185},{7,11,48}};
      g.pixels.assign (p, p+56);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{3,1,255},{6,1,255},{2,3,56},{3,3,189},{4,3,244},{5,3,244},{6,3,185},{7,3,52},{1,4,48},{2,4,239},{3,4,105},{4,4,14},{5,4,14},{6,4,108},{7,4,237},{8,4,45},{1,5,172},{2,5,119},{7,5,124},{8,5,170},{1,6,234},{2,6,32},{7,6,35},{8,6,233},{1,7,252},{2,7,8},{7,7,8},{8,7,251},{1,8,232},{2,8,35},{7,8,32},{8,8,229},{1,9,167},{2,9,127},{7,9,121},{8,9,162},{1,10,43},{2,10,239},{3,10,110},{4,10,14},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{2,11,53},{3,11,189},{4,11,244},{5,11,244},{6,11,185},{7,11,48}};
      g.pixels.assign (p, p+50);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 8;
    {
      const ClientSide::Pixel p[] = {{0,4,1},{1,4,37},{5,4,34},{6,4,1},{0,5,34},{1,5,224},{2,5,54},{4,5,54},{5,5,227},{6,5,37},{1,6,52},{2,6,223},{3,6,100},{4,6,226},{5,6,58},{2,7,100},{3,7,255},{4,7,108},{1,8,52},{2,8,225},{3,8,104},{4,8,226},{5,8,57},{0,9,36},{1,9,225},{2,9,57},{4,9,54},{5,9,227},{6,9,39},{0,10,3},{1,10,41},{5,10,39},{6,10,2}};
      g.pixels.assign (p, p+33);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 10;
    {
      const ClientSide::Pixel p[] = {{7,2,2},{8,2,59},{9,2,5},{2,3,55},{3,3,189},{4,3,244},{5,3,237},{6,3,180},{7,3,150},{8,3,157},{1,4,47},{2,4,238},{3,4,105},{4,4,14},{5,4,17},{6,4,139},{7,4,251},{8,4,47},{1,5,171},{2,5,119},{5,5,20},{6,5,187},{7,5,110},{8,5,164},{1,6,234},{2,6,32},{4,6,4},{5,6,181},{6,6,49},{7,6,32},{8,6,234},{1,7,249},{2,7,7},{4,7,143},{5,7,105},{7,7,8},{8,7,251},{1,8,222},{2,8,31},{3,8,93},{4,8,168},{7,8,32},{8,8,229},{1,9,171},{2,9,166},{3,9,209},{4,9,13},{7,9,121},{8,9,162},{1,10,53},{2,10,255},{3,10,142},{4,10,18},{5,10,13},{6,10,106},{7,10,235},{8,10,39},{1,11,154},{2,11,150},{3,11,184},{4,11,239},{5,11,244},{6,11,185},{7,11,48},{0,12,4},{1,12,55},{2,12,1}};
      g.pixels.assign (p, p+67);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{4,0,193},{5,0,63},{4,1,20},{5,1,171},{1,3,255},{7,3,255},{1,4,255},{7,4,255},{1,5,255},{7,5,255},{1,6,255},{7,6,255},{1,7,255},{7,7,255},{1,8,254},{2,8,4},{6,8,6},{7,8,253},{1,9,227},{2,9,44},{6,9,55},{7,9,219},{1,10,91},{2,10,196},{3,10,42},{4,10,8},{5,10,55},{6,10,210},{7,10,84},{2,11,70},{3,11,208},{4,11,247},{5,11,215},{6,11,102}};
      g.pixels.assign (p, p+34);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{4,0,64},{5,0,192},{4,1,171},{5,1,20},{1,3,255},{7,3,255},{1,4,255},{7,4,255},{1,5,255},{7,5,255},{1,6,255},{7,6,255},{1,7,255},{7,7,255},{1,8,254},{2,8,4},{6,8,6},{7,8,253},{1,9,227},{2,9,44},{6,9,55},{7,9,219},{1,10,91},{2,10,196},{3,10,42},{4,10,8},{5,10,55},{6,10,210},{7,10,84},{2,11,70},{3,11,208},{4,11,247},{5,11,215},{6,11,102}};
      g.pixels.assign (p, p+34);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,0,58},{4,0,255},{5,0,66},{3,1,169},{4,1,60},{5,1,169},{1,3,255},{7,3,255},{1,4,255},{7,4,255},{1,5,255},{7,5,255},{1,6,255},{7,6,255},{1,7,255},{7,7,255},{1,8,254},{2,8,4},{6,8,6},{7,8,253},{1,9,227},{2,9,44},{6,9,55},{7,9,219},{1,10,91},{2,10,196},{3,10,42},{4,10,8},{5,10,55},{6,10,210},{7,10,84},{2,11,70},{3,11,208},{4,11,247},{5,11,215},{6,11,102}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{3,1,255},{6,1,255},{1,3,255},{7,3,255},{1,4,255},{7,4,255},{1,5,255},{7,5,255},{1,6,255},{7,6,255},{1,7,255},{7,7,255},{1,8,254},{2,8,4},{6,8,6},{7,8,253},{1,9,227},{2,9,44},{6,9,55},{7,9,219},{1,10,91},{2,10,196},{3,10,42},{4,10,8},{5,10,55},{6,10,210},{7,10,84},{2,11,70},{3,11,208},{4,11,247},{5,11,215},{6,11,102}};
      g.pixels.assign (p, p+32);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{4,0,64},{5,0,192},{4,1,171},{5,1,20},{1,3,175},{2,3,68},{6,3,68},{7,3,174},{1,4,28},{2,4,202},{3,4,5},{5,4,5},{6,4,200},{7,4,27},{2,5,109},{3,5,119},{5,5,115},{6,5,108},{2,6,2},{3,6,190},{4,6,52},{5,6,187},{6,6,2},{3,7,47},{4,7,246},{5,7,47},{4,8,255},{4,9,255},{4,10,255},{4,11,255}};
      g.pixels.assign (p, p+30);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{2,5,255},{3,5,255},{4,5,254},{5,5,231},{6,5,166},{7,5,27},{1,6,255},{4,6,1},{5,6,23},{6,6,138},{7,6,190},{1,7,255},{6,7,15},{7,7,242},{1,8,255},{5,8,24},{6,8,145},{7,8,179},{1,9,255},{2,9,255},{3,9,255},{4,9,255},{5,9,229},{6,9,159},{7,9,21},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+30);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 9;
    {
      const ClientSide::Pixel p[] = {{1,3,26},{2,3,168},{3,3,233},{4,3,245},{5,3,202},{6,3,67},{1,4,182},{2,4,81},{3,4,20},{4,4,7},{5,4,80},{6,4,233},{1,5,243},{2,5,12},{5,5,107},{6,5,186},{1,6,255},{4,6,113},{5,6,150},{6,6,10},{1,7,255},{4,7,229},{5,7,78},{1,8,255},{4,8,54},{5,8,205},{6,8,184},{7,8,30},{1,9,255},{5,9,1},{6,9,109},{7,9,203},{1,10,255},{4,10,63},{5,10,7},{6,10,51},{7,10,237},{1,11,255},{4,11,206},{5,11,248},{6,11,227},{7,11,88}};
      g.pixels.assign (p, p+42);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,183},{4,2,62},{3,3,13},{4,3,155},{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+38);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,13},{4,2,208},{5,2,105},{3,3,164},{4,3,74},{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+39);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,58},{3,2,255},{4,2,65},{2,3,168},{3,3,60},{4,3,167},{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,107},{3,2,215},{4,2,50},{5,2,153},{2,3,152},{3,3,49},{4,3,214},{5,3,107},{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+42);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,255},{4,3,255},{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,1,100},{3,1,242},{4,1,98},{2,2,235},{3,2,59},{4,2,233},{2,3,101},{3,3,244},{4,3,100},{1,5,66},{2,5,211},{3,5,247},{4,5,216},{5,5,70},{1,6,212},{2,6,74},{3,6,7},{4,6,97},{5,6,222},{4,7,6},{5,7,255},{1,8,64},{2,8,203},{3,8,243},{4,8,253},{5,8,255},{1,9,231},{2,9,91},{3,9,13},{4,9,12},{5,9,255},{1,10,191},{2,10,52},{3,10,19},{4,10,129},{5,10,234},{6,10,36},{1,11,51},{2,11,238},{3,11,223},{4,11,68},{5,11,128},{6,11,245}};
      g.pixels.assign (p, p+43);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 12;
    {
      const ClientSide::Pixel p[] = {{1,5,45},{2,5,186},{3,5,238},{4,5,248},{5,5,199},{6,5,65},{7,5,185},{8,5,248},{9,5,190},{10,5,16},{1,6,203},{2,6,95},{3,6,10},{4,6,11},{5,6,117},{6,6,255},{7,6,136},{8,6,12},{9,6,137},{10,6,149},{5,7,7},{6,7,255},{7,7,24},{9,7,22},{10,7,223},{1,8,46},{2,8,180},{3,8,232},{4,8,248},{5,8,253},{6,8,255},{7,8,255},{8,8,255},{9,8,255},{10,8,248},{1,9,220},{2,9,106},{3,9,23},{4,9,5},{5,9,14},{6,9,255},{7,9,13},{1,10,186},{2,10,63},{3,10,6},{4,10,40},{5,10,163},{6,10,181},{7,10,125},{8,10,10},{9,10,94},{10,10,189},{1,11,34},{2,11,217},{3,11,246},{4,11,201},{5,11,59},{6,11,19},{7,11,186},{8,11,246},{9,11,211},{10,11,61}};
      g.pixels.assign (p, p+62);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,5,20},{2,5,181},{3,5,246},{4,5,208},{5,5,53},{1,6,160},{2,6,135},{3,6,12},{4,6,81},{5,6,205},{1,7,233},{2,7,22},{1,8,250},{2,8,4},{1,9,230},{2,9,28},{1,10,156},{2,10,146},{3,10,16},{4,10,86},{5,10,212},{1,11,19},{2,11,184},{3,11,248},{4,11,211},{5,11,56},{3,12,203},{4,12,108},{3,13,38},{4,13,228},{3,14,249},{4,14,115}};
      g.pixels.assign (p, p+32);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,183},{4,2,62},{3,3,13},{4,3,155},{1,5,25},{2,5,186},{3,5,245},{4,5,191},{5,5,16},{1,6,169},{2,6,141},{3,6,12},{4,6,136},{5,6,149},{1,7,236},{2,7,24},{4,7,22},{5,7,223},{1,8,250},{2,8,255},{3,8,255},{4,8,255},{5,8,248},{1,9,227},{2,9,13},{1,10,155},{2,10,129},{3,10,10},{4,10,99},{5,10,189},{1,11,21},{2,11,179},{3,11,243},{4,11,213},{5,11,61}};
      g.pixels.assign (p, p+35);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,13},{4,2,208},{5,2,105},{3,3,164},{4,3,74},{1,5,25},{2,5,186},{3,5,245},{4,5,191},{5,5,16},{1,6,169},{2,6,141},{3,6,12},{4,6,136},{5,6,149},{1,7,236},{2,7,24},{4,7,22},{5,7,223},{1,8,250},{2,8,255},{3,8,255},{4,8,255},{5,8,248},{1,9,227},{2,9,13},{1,10,155},{2,10,129},{3,10,10},{4,10,99},{5,10,189},{1,11,21},{2,11,179},{3,11,243},{4,11,213},{5,11,61}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,58},{3,2,255},{4,2,65},{2,3,168},{3,3,60},{4,3,167},{1,5,25},{2,5,186},{3,5,245},{4,5,191},{5,5,16},{1,6,169},{2,6,141},{3,6,12},{4,6,136},{5,6,149},{1,7,236},{2,7,24},{4,7,22},{5,7,223},{1,8,250},{2,8,255},{3,8,255},{4,8,255},{5,8,248},{1,9,227},{2,9,13},{1,10,155},{2,10,129},{3,10,10},{4,10,99},{5,10,189},{1,11,21},{2,11,179},{3,11,243},{4,11,213},{5,11,61}};
      g.pixels.assign (p, p+37);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,255},{4,3,255},{1,5,25},{2,5,186},{3,5,245},{4,5,191},{5,5,16},{1,6,169},{2,6,141},{3,6,12},{4,6,136},{5,6,149},{1,7,236},{2,7,24},{4,7,22},{5,7,223},{1,8,250},{2,8,255},{3,8,255},{4,8,255},{5,8,248},{1,9,227},{2,9,13},{1,10,155},{2,10,129},{3,10,10},{4,10,99},{5,10,189},{1,11,21},{2,11,179},{3,11,243},{4,11,213},{5,11,61}};
      g.pixels.assign (p, p+33);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,2,183},{1,2,62},{0,3,13},{1,3,155},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+11);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{1,2,13},{2,2,208},{3,2,105},{1,3,164},{2,3,74},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+12);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,2,58},{1,2,255},{2,2,65},{0,3,168},{1,3,60},{2,3,167},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+13);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 3;
    {
      const ClientSide::Pixel p[] = {{0,3,255},{2,3,255},{1,5,255},{1,6,255},{1,7,255},{1,8,255},{1,9,255},{1,10,255},{1,11,255}};
      g.pixels.assign (p, p+9);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,1},{2,3,145},{3,3,109},{4,3,159},{5,3,1},{2,4,127},{3,4,223},{4,4,83},{2,5,120},{3,5,8},{4,5,193},{5,5,20},{1,6,36},{2,6,196},{3,6,244},{4,6,230},{5,6,135},{1,7,186},{2,7,117},{3,7,12},{4,7,116},{5,7,210},{1,8,243},{2,8,11},{4,8,13},{5,8,245},{1,9,241},{2,9,13},{4,9,14},{5,9,237},{1,10,185},{2,10,117},{3,10,10},{4,10,121},{5,10,178},{1,11,38},{2,11,199},{3,11,244},{4,11,196},{5,11,34}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,107},{3,2,215},{4,2,50},{5,2,153},{2,3,152},{3,3,49},{4,3,214},{5,3,107},{0,5,7},{1,5,236},{2,5,93},{3,5,233},{4,5,234},{5,5,90},{0,6,1},{1,6,250},{2,6,139},{3,6,13},{4,6,85},{5,6,223},{1,7,255},{2,7,20},{4,7,6},{5,7,253},{1,8,255},{5,8,255},{1,9,255},{5,9,255},{1,10,255},{5,10,255},{1,11,255},{5,11,255}};
      g.pixels.assign (p, p+32);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,183},{4,2,62},{3,3,13},{4,3,155},{1,5,27},{2,5,191},{3,5,243},{4,5,194},{5,5,31},{1,6,164},{2,6,136},{3,6,13},{4,6,131},{5,6,167},{1,7,230},{2,7,23},{4,7,22},{5,7,229},{1,8,249},{2,8,3},{4,8,3},{5,8,249},{1,9,228},{2,9,22},{4,9,22},{5,9,228},{1,10,160},{2,10,132},{3,10,11},{4,10,137},{5,10,161},{1,11,26},{2,11,192},{3,11,243},{4,11,188},{5,11,26}};
      g.pixels.assign (p, p+36);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,13},{4,2,208},{5,2,105},{3,3,164},{4,3,74},{1,5,27},{2,5,191},{3,5,243},{4,5,194},{5,5,31},{1,6,164},{2,6,136},{3,6,13},{4,6,131},{5,6,167},{1,7,230},{2,7,23},{4,7,22},{5,7,229},{1,8,249},{2,8,3},{4,8,3},{5,8,249},{1,9,228},{2,9,22},{4,9,22},{5,9,228},{1,10,160},{2,10,132},{3,10,11},{4,10,137},{5,10,161},{1,11,26},{2,11,192},{3,11,243},{4,11,188},{5,11,26}};
      g.pixels.assign (p, p+37);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,58},{3,2,255},{4,2,65},{2,3,168},{3,3,60},{4,3,167},{1,5,27},{2,5,191},{3,5,243},{4,5,194},{5,5,31},{1,6,164},{2,6,136},{3,6,13},{4,6,131},{5,6,167},{1,7,230},{2,7,23},{4,7,22},{5,7,229},{1,8,249},{2,8,3},{4,8,3},{5,8,249},{1,9,228},{2,9,22},{4,9,22},{5,9,228},{1,10,160},{2,10,132},{3,10,11},{4,10,137},{5,10,161},{1,11,26},{2,11,192},{3,11,243},{4,11,188},{5,11,26}};
      g.pixels.assign (p, p+38);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,107},{3,2,215},{4,2,50},{5,2,153},{2,3,152},{3,3,49},{4,3,214},{5,3,107},{1,5,27},{2,5,191},{3,5,243},{4,5,194},{5,5,31},{1,6,164},{2,6,136},{3,6,13},{4,6,131},{5,6,167},{1,7,230},{2,7,23},{4,7,22},{5,7,229},{1,8,249},{2,8,3},{4,8,3},{5,8,249},{1,9,228},{2,9,22},{4,9,22},{5,9,228},{1,10,160},{2,10,132},{3,10,11},{4,10,137},{5,10,161},{1,11,26},{2,11,192},{3,11,243},{4,11,188},{5,11,26}};
      g.pixels.assign (p, p+40);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,255},{4,3,255},{1,5,27},{2,5,191},{3,5,243},{4,5,194},{5,5,31},{1,6,164},{2,6,136},{3,6,13},{4,6,131},{5,6,167},{1,7,230},{2,7,23},{4,7,22},{5,7,229},{1,8,249},{2,8,3},{4,8,3},{5,8,249},{1,9,228},{2,9,22},{4,9,22},{5,9,228},{1,10,160},{2,10,132},{3,10,11},{4,10,137},{5,10,161},{1,11,26},{2,11,192},{3,11,243},{4,11,188},{5,11,26}};
      g.pixels.assign (p, p+34);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,4,255},{0,7,255},{1,7,255},{2,7,255},{3,7,255},{4,7,255},{5,7,255},{6,7,255},{3,10,255}};
      g.pixels.assign (p, p+9);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{5,4,8},{6,4,10},{1,5,27},{2,5,191},{3,5,244},{4,5,195},{5,5,193},{6,5,42},{1,6,164},{2,6,136},{3,6,12},{4,6,171},{5,6,177},{1,7,230},{2,7,23},{3,7,55},{4,7,177},{5,7,233},{1,8,250},{2,8,20},{3,8,186},{4,8,16},{5,8,249},{1,9,233},{2,9,182},{3,9,48},{4,9,22},{5,9,228},{1,10,177},{2,10,169},{3,10,11},{4,10,137},{5,10,161},{0,11,49},{1,11,190},{2,11,196},{3,11,244},{4,11,188},{5,11,26},{0,12,16},{1,12,10}};
      g.pixels.assign (p, p+41);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,183},{4,2,62},{3,3,13},{4,3,155},{1,5,255},{5,5,255},{1,6,255},{5,6,255},{1,7,255},{5,7,255},{1,8,255},{5,8,255},{1,9,253},{2,9,5},{4,9,23},{5,9,255},{1,10,228},{2,10,82},{3,10,13},{4,10,140},{5,10,251},{1,11,95},{2,11,235},{3,11,235},{4,11,92},{5,11,238},{6,11,1}};
      g.pixels.assign (p, p+27);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,13},{4,2,208},{5,2,105},{3,3,164},{4,3,74},{1,5,255},{5,5,255},{1,6,255},{5,6,255},{1,7,255},{5,7,255},{1,8,255},{5,8,255},{1,9,253},{2,9,5},{4,9,23},{5,9,255},{1,10,228},{2,10,82},{3,10,13},{4,10,140},{5,10,251},{1,11,95},{2,11,235},{3,11,235},{4,11,92},{5,11,238},{6,11,1}};
      g.pixels.assign (p, p+28);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,2,58},{3,2,255},{4,2,65},{2,3,168},{3,3,60},{4,3,167},{1,5,255},{5,5,255},{1,6,255},{5,6,255},{1,7,255},{5,7,255},{1,8,255},{5,8,255},{1,9,253},{2,9,5},{4,9,23},{5,9,255},{1,10,228},{2,10,82},{3,10,13},{4,10,140},{5,10,251},{1,11,95},{2,11,235},{3,11,235},{4,11,92},{5,11,238},{6,11,1}};
      g.pixels.assign (p, p+29);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,255},{4,3,255},{1,5,255},{5,5,255},{1,6,255},{5,6,255},{1,7,255},{5,7,255},{1,8,255},{5,8,255},{1,9,253},{2,9,5},{4,9,23},{5,9,255},{1,10,228},{2,10,82},{3,10,13},{4,10,140},{5,10,251},{1,11,95},{2,11,235},{3,11,235},{4,11,92},{5,11,238},{6,11,1}};
      g.pixels.assign (p, p+25);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{3,2,13},{4,2,208},{5,2,105},{3,3,164},{4,3,74},{-1,5,17},{0,5,221},{1,5,8},{5,5,10},{6,5,220},{7,5,17},{0,6,144},{1,6,102},{5,6,117},{6,6,142},{0,7,31},{1,7,212},{2,7,1},{4,7,11},{5,7,229},{6,7,29},{1,8,166},{2,8,80},{4,8,118},{5,8,161},{1,9,49},{2,9,233},{3,9,20},{4,9,231},{5,9,44},{2,10,188},{3,10,196},{4,10,181},{2,11,71},{3,11,255},{4,11,63},{2,12,86},{3,12,200},{0,13,4},{1,13,61},{2,13,232},{3,13,65},{0,14,249},{1,14,227},{2,14,96}};
      g.pixels.assign (p, p+45);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{1,3,255},{1,4,255},{1,5,255},{2,5,84},{3,5,240},{4,5,221},{5,5,44},{1,6,254},{2,6,132},{3,6,13},{4,6,127},{5,6,181},{1,7,255},{2,7,24},{4,7,21},{5,7,236},{1,8,255},{2,8,3},{4,8,4},{5,8,250},{1,9,255},{2,9,23},{4,9,21},{5,9,235},{1,10,254},{2,10,125},{3,10,13},{4,10,127},{5,10,177},{1,11,254},{2,11,79},{3,11,236},{4,11,219},{5,11,39},{1,12,255},{1,13,255},{1,14,255}};
      g.pixels.assign (p, p+37);
    }
    Global::graphFont.glyphs.push_back (g);
  }
  {
    ClientSide::Glyph g;
    g.advance = 7;
    {
      const ClientSide::Pixel p[] = {{2,3,255},{4,3,255},{-1,5,17},{0,5,221},{1,5,8},{5,5,10},{6,5,220},{7,5,17},{0,6,144},{1,6,102},{5,6,117},{6,6,142},{0,7,31},{1,7,212},{2,7,1},{4,7,11},{5,7,229},{6,7,29},{1,8,166},{2,8,80},{4,8,118},{5,8,161},{1,9,49},{2,9,233},{3,9,20},{4,9,231},{5,9,44},{2,10,188},{3,10,196},{4,10,181},{2,11,71},{3,11,255},{4,11,63},{2,12,86},{3,12,200},{0,13,4},{1,13,61},{2,13,232},{3,13,65},{0,14,249},{1,14,227},{2,14,96}};
      g.pixels.assign (p, p+42);
    }
    Global::graphFont.glyphs.push_back (g);
  }
}

#endif

}
