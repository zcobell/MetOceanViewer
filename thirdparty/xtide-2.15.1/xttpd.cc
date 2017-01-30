// $Id: xttpd.cc 6280 2016-02-23 00:33:39Z flaterco $

/*  xttpd  XTide web server.

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
#include "libxtide/Graph.hh"
#include "libxtide/PixelatedGraph.hh"
#include "libxtide/RGBGraph.hh"
#include "libxtide/SVGGraph.hh"
using namespace libxtide;
#include "ZoneIndex.hh"

#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>

#if systemd
#include <systemd/sd-daemon.h>
#endif


// These browsers nowadays can get pretty verbose.
static const size_t bufsize (10000);


// Nasty global variables.
static Dstr webmaster;
static ZoneIndex zoneIndex;
static bool zoneinfoIsNotHorriblyObsolete;


namespace TimeControl {
  enum Mode {year,	// year
	     month,	// year, month
	     day	// year, month, day
  };
}


// Fedora package compiles generate warnings for invoking write
// without checking the return.  As it happens there is not much more
// we can do if write starts to fail.
static void checkedWrite (int fd, const void *buf, size_t count) {
  ssize_t writeReturn (write (fd, buf, count));
  if (writeReturn < 0)
    exit (0);
}


static void setWebmaster () {
  webmaster = getenv ("XTTPD_FEEDBACK");
  #ifdef webmasteraddr
    if (webmaster.isNull())
      webmaster = webmasteraddr;
  #endif
  if (webmaster == "dave@flaterco.com")
    webmaster = (char *)NULL;  // I can't handle the AOLers all by myself.
  if (webmaster.isNull()) {
    // Guess the local webmaster.  (Guessing the FQDN is harder!)
    hostent *hp;
    // On Suns, need struct keyword to avoid confusion with a global variable
    // (extern struct utsname utsname declared in sys/utsname.h).
    struct utsname foo;
    uname (&foo);
    if (!(hp = (hostent *) gethostbyname (foo.nodename)))
      webmaster = foo.nodename;
    else
      webmaster = hp->h_name;
    if (webmaster.strchr('.') == -1) {
      // getdomainname() is useless
      Dstr domain;
      FILE *fp;
      if ((fp = fopen ("/etc/defaultdomain", "r"))) {
	domain.scan (fp);
	fclose (fp);
	webmaster += '.';
	webmaster += domain;
      } else if ((fp = fopen ("/etc/domain", "r"))) {
	domain.scan (fp);
	fclose (fp);
	webmaster += '.';
	webmaster += domain;
      } else if ((fp = fopen ("/etc/resolv.conf", "r"))) {
	Dstr buf, srch;
	bool found=false;
	while (!(buf.getline(fp)).isNull()) {
	  buf /= domain;
	  if (domain == "domain") {
	    buf /= domain;
	    webmaster += '.';
	    webmaster += domain;
	    found = true;
	    break;
	  } else if (srch.isNull() && domain == "search") {
            buf /= srch;  // Just the first one.
          }
	}
	fclose (fp);
	if (!found && !srch.isNull()) {
	  webmaster += '.';
	  webmaster += srch;
	}
      }
    }
    if (webmaster %= "www.")
      webmaster /= 4;
    webmaster *= "webmaster@";
  }
  webmaster *= "<a href=\"mailto:";
  webmaster += "\">";
}


#if !systemd
static const bool parseAddress (sockaddr_storage &addr,
				constString arg) {
  assert (arg);

  // getaddrinfo parses addresses but not port numbers.
  // 1. If it starts with hyphen then it's a setting.
  // 2. If it's a null string or a single slash, reject.
  // 3. If it contains a slash then parse as address/port.
  // 4. If it's entirely numeric then parse as port.
  // 5. Else parse as address or hostname.

  bool gotAddr=false, gotPort=false;
  unsigned short portNumber;
  Dstr darg(arg);
  char *caddr = NULL;

  if (darg[0] == '-')
    return false;
  else if (darg.length()==0 || darg == "/")
    Global::barf (Error::PORT_FAIL);
  else if (darg.contains('/')) {
    // Address/port
    int i = darg.strrchr('/');
    assert (i >= 0);
    if ((unsigned)i+1 < darg.length()) {     // Maybe they just said addr/
      if (sscanf (darg.ascharfrom(i+1), "%hu", &portNumber) == 1)
        gotPort = true;
      else
        Global::barf (Error::PORT_FAIL);
    }
    if (i > 0) {                             // Maybe they just said /port
      darg -= i;
      caddr = darg.aschar();
      gotAddr = true;
    }
  } else {
    bool isNum = true;
    for (unsigned i=0; i<darg.length(); ++i) {
      const char c = darg[i];
      if (c < '0' || c > '9') {
        isNum = false;
        break;
      }
    }
    if (isNum) {
      // Port number
      if (sscanf (arg, "%hu", &portNumber) == 1)
        gotPort = true;
      else
        Global::barf (Error::PORT_FAIL);
    } else {
      // Address or host name
      caddr = darg.aschar();
      gotAddr = true;
    }
  }

  if (!gotPort)
    portNumber = 80;

  if (gotAddr) {
    struct addrinfo hint, *res;
    memset(&hint, 0, sizeof hint);
    hint.ai_family   = PF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;
    int ret = getaddrinfo(caddr, NULL, &hint, &res);
    if (ret) {
      Dstr details ("getaddrinfo: ");
      details += gai_strerror(ret);
      details += "\nThe offending address was ";
      details += caddr;
      Global::barf (Error::ADDRESS_FAIL, details);
    }
    assert (res);
    assert (res->ai_addrlen <= sizeof(addr));
    memcpy (&addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo (res);
    switch (addr.ss_family) {
    case AF_INET:
      ((sockaddr_in *)&addr)->sin_port = htons(portNumber);
      break;
    case AF_INET6:
      ((sockaddr_in6 *)&addr)->sin6_port = htons(portNumber);
      break;
    default:
      Global::barf (Error::CANT_GET_SOCKET, "Surprising family in parseAddress.");
    }
  } else {
    memset (&addr, 0, sizeof(addr));
    sockaddr_in *sin = (sockaddr_in *)&addr;
    sin->sin_family      = AF_INET;
    sin->sin_port        = htons(portNumber);
    sin->sin_addr.s_addr = INADDR_ANY;
  }

  return true;
}


static void setupSocket (sockaddr_storage &addr,
                         int &s) {
  if ((s = socket (addr.ss_family, SOCK_STREAM, 0)) == -1) {
    Dstr details ("socket: ");
    details += strerror (errno);
    details += '.';
    Global::barf (Error::CANT_GET_SOCKET, details);
  }{
    int tmp (1);
    if (setsockopt (s, SOL_SOCKET, SO_REUSEADDR,
	  	    (char *)&tmp, sizeof(tmp)) < 0) {
      Dstr details ("setsockopt: ");
      details += strerror (errno);
      details += '.';
      Global::barf (Error::CANT_GET_SOCKET, details);
    }
  }{
    linger li;
    li.l_onoff = 1;
    li.l_linger = 6000; // Hundredths of seconds (1 min)
    if (setsockopt (s, SOL_SOCKET, SO_LINGER,
                   (char *)&li, sizeof(linger)) < 0) {
      Global::xperror ("setsockopt");
    }
  }

  // 2016-02-21
  // Manfred Antar reported "bind: Invalid argument" occurring on FreeBSD and
  // OS X as a regression from 2.14.  The FreeBSD man page for bind states
  // that EINVAL can be raised if "the addrlen argument is not a valid length
  // for the address family."  Apparently, we have to fake the size to match
  // the applicable narrowed type, rather than passing the true size of
  // sockaddr_storage.  The error does not occur with linux libc.
  const socklen_t addrlen (addr.ss_family==AF_INET6 ? sizeof(sockaddr_in6)
                                                    : sizeof(sockaddr_in));
  assert (addrlen <= sizeof(addr));

  bool bound = (bind(s,(sockaddr *)&addr,addrlen)==0);
  int yerrno = errno;
  if (!bound && addr.ss_family == AF_INET6) {

    // Here comes trouble.

    // The Linux kernel (bind syscall) will sometimes refuse to bind an
    // address (likely a link-local address) unless it is told which network
    // interface to bind it on.  Specifically, it expects you to put the
    // interface index in the sin6_scope_id field of the sockaddr_in6.  Apps
    // are now supposed to implement the convoluted process of looking up the
    // interface index given an address (see example at
    // http://stackoverflow.com/questions/13504934/binding-sockets-to-ipv6-addresses)
    // or else require the user to specify a network interface.

    // Network interface indices are just serial numbers that are assigned
    // starting at 1.  Typically, index 1 will be loopback, index 2 will be
    // the first or only real network interface, and any extra interfaces
    // will be index 3 and up.

    // The scope ID is a *different* magic number (see
    // https://en.wikipedia.org/wiki/IPv6_address#IPv6_address_scopes).  It
    // so happens that the scope ID corresponding to link-local scope is 2.
    // 3 is not a defined scope ID, but 4 and 5 are.  So we have here a
    // recipe for truly epic confusion:  putting the scope ID in the scope ID
    // field will work a lot of the time but not always, and the error codes
    // from bind are nonspecific about the problem.

    for (unsigned magic=4; !bound && magic>0; --magic) {
      ((sockaddr_in6 *)&addr)->sin6_scope_id = magic;
      bound = (bind(s,(sockaddr *)&addr,addrlen)==0);
    }
  }
  // Report the error from the first failure so that it won't be obscured by
  // other errors caused by the magic number guessing.
  if (!bound) {
    if (yerrno == EADDRINUSE) {
      Dstr details ("Socket already in use.");
      Global::barf (Error::CANT_GET_SOCKET, details);
    } else {
      Dstr details ("bind: ");
      details += strerror (yerrno);
      details += '.';
      Global::barf (Error::CANT_GET_SOCKET, details);
    }
  }
  // backlog was 0, but this hung Digital Unix.  5 is the limit for BSD.
  if (listen (s, 5) == -1) {
    Dstr details ("listen: ");
    details += strerror (errno);
    details += '.';
    Global::barf (Error::CANT_GET_SOCKET, details);
  }
}
#endif


static void addDisclaimer (Dstr &foo) {
  foo += "<center><h2> NOT FOR NAVIGATION </h2></center>\n\
<blockquote>\n\
<p>This program is distributed in the hope that it will be useful, but\n\
WITHOUT ANY WARRANTY; without even the implied warranty of\n\
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.&nbsp; The author\n\
assumes no liability for damages arising from use of these predictions.&nbsp;\n\
They are not certified to be correct, and they\n\
do not incorporate the effects of tropical storms, El Ni&ntilde;o,\n\
seismic events, subsidence, uplift, or changes in global sea level.\n\
</p></blockquote>\n";
}


static void endPage (int s) {
  Dstr pageEnd ("<hr>");
  pageEnd += "<p> <a href=\"/\">Start over</a> <br>\n";
  pageEnd += "<a href=\"/index.html\">Master index</a> <br>\n";
  pageEnd += "<a href=\"/zones/\">Zone index</a> <br>\n";
  pageEnd += "<a href=\"http://www.flaterco.com/xtide/\">XTide software</a> <br>\n";
  pageEnd += webmaster;
  pageEnd += "Feedback</a> <br>\n";
  pageEnd += "<a href=\"/tricks.html\">Hints and tricks</a></p>\n\
</body>\n\
</html>\n";
  checkedWrite (s, pageEnd.aschar(), pageEnd.length());
  close (s);
  exit (0);
}


static void unimplementedBarf (int s) {
  constString barfMessage ("\
HTTP/1.0 501 Not Implemented\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> HTTP Error </title></head>\n\
<body bgcolor=\"FFFFFF\"><h1> HTTP Error </h1>\n\
<p>So sorry!&nbsp; This server cannot satisfy your request.</p>\n");

  checkedWrite (s, barfMessage, strlen (barfMessage));
  endPage (s);
}


static void badRequestBarf (int s, const Dstr &message) {
  Dstr barfMessage ("\
HTTP/1.0 400 Bad Request\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title>");
  barfMessage += message;
  barfMessage += "</title></head>\n\
<body bgcolor=\"FFFFFF\"><h1>";
  barfMessage += message;
  barfMessage += "</h1>\n\
<p>So sorry!&nbsp; This server cannot satisfy your request.</p>\n";

  checkedWrite (s, barfMessage.aschar(), barfMessage.length());
  endPage (s);
}


static void notFoundBarf (int s) {
  constString barfMessage ("\
HTTP/1.0 404 Not Found\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> Not Found </title></head>\n\
<body bgcolor=\"FFFFFF\"><h1> Not Found </h1>\n\
<p>So sorry!&nbsp; This server cannot satisfy your request.</p>\n");

  checkedWrite (s, barfMessage, strlen (barfMessage));
  endPage (s);
}


static void tooLongBarf (int s) {
  constString barfMessage ("\
HTTP/1.0 413 Request Entity Too Large\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title>Request Entity Too Large</title></head>\n\
<body bgcolor=\"FFFFFF\"><h1>Request Entity Too Large</h1>\n\
<p>Sorry, loser!&nbsp; Your buffer overflow attack didn't work.</p>\n");

  checkedWrite (s, barfMessage, strlen (barfMessage));
  endPage (s);
}


static void rootPage (int s) {
  Dstr rootPageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> XTide Tide Prediction Server </title></head>\n\
<body bgcolor=\"FFFFFF\"><center><h1> XTide Tide Prediction Server </h1></center>\n\
\n\
<center>\n\
<p>Copyright (C) 1998\n\
David Flater.</p>\n\
</center>\n");
  addDisclaimer (rootPageText);
  rootPageText += "<blockquote>\n\
<p>\n\
While this program is maintained by David Flater, the web site\n\
itself is not.&nbsp; Please direct feedback about problems with the web site to\n\
the local ";
  rootPageText += webmaster;
  rootPageText += "webmaster</a>.</p></blockquote>\n\
<p> XTide can provide tide predictions for thousands of places, but first you have to select a location.&nbsp;\n\
There are three ways to do this.&nbsp; One\n\
way is just to enter its name here, and click on Search: </p>\n\
<p><form method=get action=\"/query\">\n\
<input name=\"location\" type=text size=\"48\">\n\
<input type=submit value=\"Search\"> <input type=reset value=\"Clear\">\n\
</form></p>\n\
\n\
<p> Your other choices are the <a href=\"/zones/\">zone index</a>, which\n\
indexes locations by time zone, and \n\
the <a href=\"index.html\">master index</a>, which is just one big list\n\
of every location supported by this server.&nbsp; The master index could be\n\
very large, so if you are on a slow connection, don't use it.</p>\n";

  rootPageText += "<p>Version info:<br>";
  rootPageText += XTideVersionString;
  Dstr hfileIDs;
  Global::stationIndex().hfileIDs (hfileIDs);
  if (!(hfileIDs.isNull())) {
    rootPageText += "<br>\n";
    rootPageText += hfileIDs;
  }
  rootPageText += "</p>\n";

  checkedWrite (s, rootPageText.aschar(), rootPageText.length());
  endPage (s);
}


static void indexPage (int s) {
  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> XTide Master Index </title></head>\n\
<body bgcolor=\"FFFFFF\"><center><h1> XTide Master Index </h1></center>\n\
\n\
<p>Click on any location to get tide predictions.</p>\n");

  Dstr temp;
  Global::stationIndex().print (temp, Format::HTML, StationIndex::xttpdStyle);
  pageText += temp;
  checkedWrite (s, pageText.aschar(), pageText.length());
  endPage (s);
}


static void tricks (int s) {
  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> XTide hints and tricks </title></head>\n\
<body bgcolor=\"FFFFFF\"><center><h1> XTide hints and tricks </h1></center>\n\
\n\
<p> This web server uses numerically indexed URLs to load prediction\n\
pages.&nbsp; Those URLs look like this:</p>\n\
\n\
<pre>http://whatever/locations/nnn.html</pre>\n\
\n\
<p>For convenience, you can also load a prediction page with a\n\
URL of the following form: </p>\n\
\n\
<pre>http://whatever/locations/name</pre>\n\
\n\
<p>You will receive an error if the name-based lookup gives ambiguous results,\n\
but these URLs have the advantage that they do not need to change every time\n\
the tide data are updated.</p>");

  checkedWrite (s, pageText.aschar(), pageText.length());
  endPage (s);
}


static void addTimeControl (TimeControl::Mode type,
                            Dstr &text,
                            const Dstr &url,
                            unsigned y,
                            unsigned m,
                            unsigned d,
                            bool allowForms) {
  text += "<p> <form method=get action=\"";
  text += url;
  text += "\">\nYear: <select name=\"y\">\n";
  unsigned i;
  for (i=Global::dialogFirstYear; i<=Global::dialogLastYear; ++i) {
    if (i == y)
      text += "<option selected>";
    else
      text += "<option>";
    text += i;
    text += "\n";
  }
  text += "</select>\n";

  if (type != TimeControl::year) {
    text += "Month: <select name=\"m\">\n";
    for (i=1; i<=12; ++i) {
      if (i == m)
        text += "<option selected>";
      else
        text += "<option>";
      text += i;
      text += "\n";
    }
    text += "</select>\n";
  }

  if (type == TimeControl::day) {
    text += "Day: <select name=\"d\">\n";
    for (i=1; i<=31; ++i) {
      if (i == d)
        text += "<option selected>";
      else
        text += "<option>";
      text += i;
      text += "\n";
    }
    text += "</select>\n";
  }

  if (allowForms) {
    text += "Output: <select name=\"f\">\n";
    text += "<option selected>Web page</option>\n";
    text += "<option>iCalendar</option>\n";
    text += "</select>\n";
  }

  text += "<input type=submit value=\"Go\"></form></p>";
}


static const Timestamp parseTimeControl (const Dstr &filename,
					 const Dstr &timezone,
					 int s,
					 Dstr &tc_out,
					 Format::Format &form_out) {
  tc_out = (char *)NULL;
  Timestamp ret ((time_t)time(NULL));
  form_out = Format::HTML;
  int i;
  if ((i = filename.strrchr ('?')) != -1) {
    unsigned y, m=1, d=1;
    if ((sscanf (filename.ascharfrom(i), "?y=%u&m=%u&d=%u", &y, &m, &d)) >= 1) {
      char temp[80];
      sprintf (temp, "%4u-%02u-%02u 00:00", y, m, d);
      ret = Timestamp (temp, timezone);
      if (ret.isNull()) {
        Global::log ("Invalid client time control: ", filename, LOG_NOTICE);
        badRequestBarf (s, "Invalid time control");
      }
      tc_out = filename.ascharfrom(i);
    }
    if ((i = filename.strstr ("&f=")) != -1) {
      char formsel;
      sscanf (filename.ascharfrom(i), "&f=%c", &formsel);
      if (formsel != 'W' && formsel != 'i')
        unimplementedBarf (s);
      if (formsel == 'i')
        form_out = Format::iCalendar;
    }
  }
  return ret;
}


static void loadLocationPage (int s, const Dstr &filename, unsigned long i) {
  StationIndex &stationIndex (Global::stationIndex());
  Station *station = stationIndex[i]->load();

  // 20110730  IE9 plays dumb on SVG if <!DOCTYPE html> is not present.

  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<!DOCTYPE html>\n\
<html>\n\
<head><title> ");

  pageText += station->name;
  pageText += "</title></head>\n<body bgcolor=\"FFFFFF\">\n<center><h1> ";
  pageText += station->name;
  pageText += " <br>\n";

  Dstr text_out, timeControlString;
  Format::Format form;
  Timestamp startTime (parseTimeControl (filename, station->timezone, s,
					 timeControlString, form));
  Dstr myurl ("/locations/");
  myurl += i;
  myurl += ".html";
  tm startTimeTm (startTime.tmStruct (station->timezone));
  unsigned y = startTimeTm.tm_year + 1900;
  unsigned m = startTimeTm.tm_mon + 1;
  unsigned d = startTimeTm.tm_mday;

  if (timeControlString.isNull())
    pageText += "Local time:  ";
  else
    pageText += "Requested time:  ";
  Dstr nowbuf;
  startTime.print (nowbuf, station->timezone);
  pageText += nowbuf;
  pageText += " </h1></center>\n";

  if (!zoneinfoIsNotHorriblyObsolete) {
    pageText += "<p> Warning:  this host machine is using an obsolete time zone\n\
database.&nbsp; Summer Time (Daylight Savings Time) adjustments will not be done\n\
for some locations. </p>\n";
  }

  // Query browser for SVG support; fall back to PNG if not supported.
  Dstr repeatText (timeControlString);
  repeatText += "\\\" width=";
  repeatText += Global::settings["gw"].u;
  repeatText += " height=";
  repeatText += Global::settings["gh"].u;
  repeatText += " /></p>\");\n";
  pageText += "<script type=\"text/javascript\">\n";
  pageText += "if (document.implementation.hasFeature(\"http://www.w3.org/TR/SVG11/feature#BasicStructure\", \"1.1\")) {\n";
  // 20110730    Firefox 5     Internet Explorer 9
  // img        Ugly scaling           OK
  // object         OK                FAIL
  // embed          OK                 OK
  pageText += "\tdocument.write(\"<p><embed type=\\\"image/svg+xml\\\" alt=\\\"SVG graph\\\" src=\\\"/graphs/";
  pageText += i;
  pageText += ".svg";
  pageText += repeatText;
  pageText += "} else {\n";
  pageText += "\tdocument.write(\"<p><img alt=\\\"PNG graph\\\" src=\\\"/graphs/";
  pageText += i;
  pageText += ".png";
  pageText += repeatText;
  pageText += "}\n";
  pageText += "</script>\n";

  pageText += "<p><pre>\n";

  Configurable &picfg = Global::settings["pi"];
  station->plainMode (text_out,
                      startTime,
                      startTime + Interval(picfg.u*DAYSECONDS),
                      Format::text);
  pageText += text_out;
  pageText += "</pre></p>\n";

  pageText += "<h2> Time Control </h2>\n";
  addTimeControl (TimeControl::day, pageText, myurl, y, m, d, false);
  pageText += "<p> Note:&nbsp; If your browser returns a blank page or a \"no data\"\n\
error, then the predictions that you requested are not available.</p>\n";

  pageText += "<h2>Monthly Tide Calendars</h2>\n";
  Dstr monthurl ("/calendar/month/");
  monthurl += i;
  monthurl += ".ics";
  addTimeControl (TimeControl::month, pageText, monthurl, y, m, d, true);

  pageText += "<h2>Yearly Tide Calendars</h2>\n";
  Dstr yearurl ("/calendar/year/");
  yearurl += i;
  yearurl += ".ics";
  addTimeControl (TimeControl::year, pageText, yearurl, y, m, d, true);

  pageText += "<p>Selecting iCalendar output will cause supporting browsers to add events to your calendar.&nbsp; Before adding an entire month or year of events, please try this <a href=\"/calendar/day/";
  pageText += i;
  pageText += ".ics?y=";
  pageText += y;
  pageText += "&m=";
  pageText += m;
  pageText += "&d=";
  pageText += d;
  pageText += "&f=i\">one-day iCalendar test</a> to see how your calendar will react.&nbsp; The events that you get should have no duration and should not block out any time as \"busy\" time.</p>\n";

  pageText += "<h2>About this station</h2>\n";
  Dstr aboutbuf;
  station->aboutMode (aboutbuf, Format::HTML, Global::codeset);
  pageText += aboutbuf;

  addDisclaimer (pageText);
  checkedWrite (s, pageText.aschar(), pageText.length());
  endPage (s);
}


// Remove HTTP mangling from query string.
static void demangle (Dstr &s, int sock) {
  Dstr buf;
  unsigned i = 0;
  while (i<s.length()) {
    if (s[i] == '+') {
      buf += ' ';
      ++i;
    } else if (s[i] == '%') {
      char tiny[3];
      unsigned temp;
      ++i;
      tiny[0] = s[i++];
      tiny[1] = s[i++];
      tiny[2] = '\0';
      if (sscanf (tiny, "%x", &temp) != 1) {
        Global::log ("Really nasty URL caught in demangle....", LOG_NOTICE);
        notFoundBarf (sock);
      }
      buf += (char)temp;
    } else
      buf += s[i++];
  }
  s = buf;
}


static void exactLocation (int s, const Dstr &filename, const Dstr &loc) {
  unsigned long count (0);
  long match (-1);
  StationIndex &stationIndex (Global::stationIndex());
  for (unsigned long i=0; i<stationIndex.size(); ++i) {
    if (stationIndex[i]->name %= loc) {
      if (match == -1)
        match = i;
      else
        match = -2;
    }
    ++count;
  }

  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> Exact Query Error </title></head>\n<body bgcolor=\"FFFFFF\">\n<center>\n\
<h1> Exact Query Error </h1></center>\n");

  switch (match) {
  case -1:
    pageText += "<p>No locations matched your query.</p>";
    break;
  case -2:
    pageText += "<p>More than one location matched your query.</p>\n";
    break;
  default:
    loadLocationPage (s, filename, (unsigned long)match);
    return;
  }
  checkedWrite (s, pageText.aschar(), pageText.length());
  endPage (s);
}


static void badClientLocation (const Dstr &filename) {
  Global::log ("Bad client location: ", filename, LOG_NOTICE);
}


static void rangeError (const Dstr &filename) {
  Global::log ("Bad client location (range error): ", filename, LOG_NOTICE);
}


static void locationPage (int s, const Dstr &filename) {
  unsigned long i;
  Dstr loc (filename);
  loc /= strlen("/locations/");
  if (!isdigit(loc[0])) {
    demangle (loc, s);
    exactLocation (s, filename, loc);
    return;
  }

  if (sscanf (loc.aschar(), "%lu.html", &i) != 1) {
    badClientLocation (filename);
    notFoundBarf (s);
  }

  if (i >= Global::stationIndex().size()) {
    rangeError (filename);
    notFoundBarf (s);
  }
  loadLocationPage (s, filename, i);
}


static void calendar (int s, const Dstr &filename) {
  unsigned long i;
  TimeControl::Mode whichcal (TimeControl::year);
  if (sscanf (filename.aschar(), "/calendar/year/%lu.ics", &i) != 1) {
    whichcal = TimeControl::month;
    if (sscanf (filename.aschar(), "/calendar/month/%lu.ics", &i) != 1) {
      whichcal = TimeControl::day;
      if (sscanf (filename.aschar(), "/calendar/day/%lu.ics", &i) != 1) {
        badClientLocation (filename);
        notFoundBarf (s);
      }
    }
  }

  StationIndex &stationIndex (Global::stationIndex());
  if (i >= stationIndex.size()) {
    rangeError (filename);
    notFoundBarf (s);
  }
  Station *station = stationIndex[i]->load ();

  Dstr timeControlString;
  Format::Format form;
  Timestamp startTime (parseTimeControl (filename, station->timezone, s,
					 timeControlString, form));
  tm startTimeTm (startTime.tmStruct (station->timezone));
  unsigned y (startTimeTm.tm_year + 1900);
  unsigned m (startTimeTm.tm_mon + 1);
  unsigned d (startTimeTm.tm_mday);

  switch (whichcal) {
  case TimeControl::year:
    assert (m == 1);
  case TimeControl::month:
    assert (d == 1);
  case TimeControl::day:
    ; // Shut up compiler warning
  }

  Timestamp endTime;
  {
    // Since we are no longer using libc mktime, we can't submit
    // abnormalized time strings and expect a friendly response.
    char temp[80];
    switch (whichcal) {
    case TimeControl::year:
      sprintf (temp, "%4u-01-01 00:00", y+1);
      endTime = Timestamp (temp, station->timezone);
      break;
    case TimeControl::month:
      if (m == 12)
        m=1, ++y;
      else
        ++m;
      sprintf (temp, "%4u-%02u-01 00:00", y, m);
      endTime = Timestamp (temp, station->timezone);
      break;
    case TimeControl::day:
      // Psyche!
      endTime = startTime;
      endTime.nextDay (station->timezone);
      break;
    }
    if (endTime.isNull()) {
      Global::log ("Invalid calendar end time for ", filename, LOG_NOTICE);
      badRequestBarf (s, "End of time");
    }
  }

  Dstr pageText ("HTTP/1.0 200 OK\nMIME-version: 1.0\n");
  if (form == 'h') {
    pageText += "Content-type: text/html\n\n<html>\n<head><title>";
    pageText += station->name;
    pageText += "</title></head>\n<body bgcolor=\"FFFFFF\">\n";
    if (!zoneinfoIsNotHorriblyObsolete) {
      pageText += "<p> Warning:  this host machine is using an obsolete time zone\n\
  database.&nbsp; Summer Time (Daylight Savings Time) adjustments will not be done\n\
  for some locations. </p>\n";
    }
    pageText += "<p>\n";
  } else {
    // There is no RFC defining the rules for HTML transport.
    // Extrapolating from RFCs 2445, 2446, 2447.
    pageText += "Content-Type:text/calendar; method=publish; charset=iso-8859-1; \n component=vevent\nContent-Transfer-Encoding: 8bit\n\n";
  }

  Dstr text_out;
  station->calendarMode (text_out, startTime, endTime, Mode::calendar, form);
  pageText += text_out;
  if (form == Format::HTML) {
    pageText += "</P>\n";
    addDisclaimer (pageText);
  }

  checkedWrite (s, pageText.aschar(), pageText.length());
  if (form == Format::HTML)
    endPage (s);
  else {
    close (s);
    exit (0);
  }
}


static void favicon (int s) {

  static uint8_t favicon[318] = {0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10,
  0x10, 0x10, 0x00, 0x01, 0x00, 0x04, 0x00, 0x28, 0x01, 0x00, 0x00, 0x16, 0x00,
  0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
  0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xEB, 0xCE,
  0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x20, 0x01, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x11,
  0x11, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
  0x22, 0x22, 0x21, 0x11, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x11, 0x01,
  0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x21, 0x01, 0x11, 0x11, 0x11, 0x22, 0x22,
  0x22, 0x21, 0x01, 0x11, 0x11, 0x11, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, 0x01,
  0x11, 0x22, 0x22, 0x22, 0x22, 0x01, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22,
  0x02, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22, 0x02, 0x21, 0x11, 0x11, 0x22,
  0x22, 0x22, 0x22, 0x02, 0x22, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x21, 0x11, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x11, 0x22, 0x22, 0x22,
  0x22, 0x22, 0x22, 0x22, 0x21, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  Dstr pageText
    ("HTTP/1.0 200 OK\nMIME-version: 1.0\nContent-type: image/x-icon\n\n");
  checkedWrite (s, pageText.aschar(), pageText.length());
  checkedWrite (s, favicon, sizeof(favicon));
  close (s);
  exit (0);
}


// The user_io_ptr feature of libpng doesn't seem to work.
static int PNGSocket;

static void writePNGToSocket (png_structp png_ptr unusedParameter,
                              png_bytep b_ptr,
                              png_size_t sz) {
  checkedWrite (PNGSocket, b_ptr, sz);
}


static void graphImage (int s, const Dstr &filename) 
{
  unsigned long i;

  // Failure to match the stuff following the %lu has no observable impact,
  // so it doesn't help to put %lu.png or %lu.svg.
  if (sscanf (filename.aschar(), "/graphs/%lu", &i) != 1) {
    badClientLocation (filename);
    notFoundBarf (s);
  }
  bool isSVG = filename.contains(".svg");

  StationIndex &stationIndex (Global::stationIndex());
  if (i >= stationIndex.size()) {
    rangeError (filename);
    notFoundBarf (s);
  }
  Station *station = stationIndex[i]->load();

  Dstr timeControlString;
  Format::Format form;
  Timestamp startTime = parseTimeControl (filename, station->timezone, s,
    timeControlString, form);

  Dstr pageText
     ("HTTP/1.0 200 OK\nMIME-version: 1.0\nPragma: no-cache\n");
  checkedWrite (s, pageText.aschar(), pageText.length());

  if (!isSVG) {
     Dstr contentType ("Content-type: image/png\n\n");
     checkedWrite (s, contentType.aschar(), contentType.length());
     
     RGBGraph g (Global::settings["gw"].u, Global::settings["gh"].u);
     g.drawTides (station, startTime);
     PNGSocket = s;
     g.writeAsPNG (writePNGToSocket);
  } else {
     Dstr contentType ("Content-type: image/svg+xml\n\n");
     checkedWrite (s, contentType.aschar(), contentType.length());
     SVGGraph svg (Global::settings["gw"].u, Global::settings["gh"].u);
     Dstr text_out;
     svg.drawTides (station, startTime);
     svg.print (text_out);
     checkedWrite (s, text_out.aschar(), text_out.length());
  }

  close (s);
  exit (0);
}


static void zones (int s, const Dstr &filename) {
  Dstr zone (filename);
  zone /= strlen("/zones/");
  Dstr title;
  ZoneIndex::ZInode *dn = zoneIndex[zone];
  if (!dn) {
    Global::log ("Bad zone: ", filename, LOG_NOTICE);
    notFoundBarf (s);
  }
  title = "Zone ";
  if (zone.length())
    title += zone;
  else
    title += "Index";

  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title> ");
  pageText += title;
  pageText += "</title></head>\n<body bgcolor=\"FFFFFF\">\n<center><h1> ";
  pageText += title;
  pageText += " </h1></center>\n";

  if (!(dn->subzones.empty())) {
    ZoneIndex::ZImap &sz = dn->subzones;
    pageText += "\
<p> Most zones consist of a region followed by a city name.&nbsp; Choose the\n\
zone that matches the country and time zone of the location that you want.&nbsp;\n\
For example:  places on the east coast of the U.S. are in zone\n\
:America/New_York; places in New Brunswick, Canada are in zone\n\
:America/Halifax.</p><ul>\n";
    for (ZoneIndex::ZImap::iterator it = sz.begin(); it != sz.end(); ++it) {
      pageText += "<li><a href=\"/zones/";
      pageText += it->first;
      pageText += "\">";
      pageText += it->first;
      pageText += "</a></li>\n";
    }
    pageText += "</ul>\n";
  }
  if (!(dn->stationIndex.empty())) {
    pageText += "<p>Click on any location to get tide predictions.</p>\n";
    Dstr temp;
    dn->stationIndex.print (temp, Format::HTML, StationIndex::xttpdStyle);
    pageText += temp;
  }
  if (zone.length()) {
    Dstr upzone (filename);
    upzone -= upzone.length()-1;
    upzone -= upzone.strrchr('/')+1;
    pageText += "<p> <a href=\"";
    pageText += upzone;
    pageText += "\">Back up</a></p>\n";
  }
  checkedWrite (s, pageText.aschar(), pageText.length());
  endPage (s);
}


static void query (int s, const Dstr &filename) {
  Dstr locq (filename);
  locq /= strlen("/query?location=");
  demangle (locq, s);
  locq.trim_head(); // why needed?

  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/html\n\
\n\
<html>\n\
<head><title>Query Results</title></head>\n<body bgcolor=\"FFFFFF\">\n<center>\n\
<h1>Query Results</h1></center>\n");

  StationIndex &stationIndex (Global::stationIndex());
  StationIndex queryResult;
  stationIndex.query (locq, queryResult, StationIndex::percentEqual);

  if (queryResult.empty()) {
    pageText += "<p>Regular query found no matches; using more aggressive (substring) query.</p>\n";
    stationIndex.query (locq, queryResult, StationIndex::contains);
  }

  if (queryResult.empty()) {
    pageText += "<p>No locations matched your query.&nbsp; However, that might\n\
just mean that something is spelled strangely in the XTide database.&nbsp; To\n\
make sure that you aren't missing a location that is actually in there,\n\
you should check the indexes linked below.</p>\n";
  } else {
    pageText += "<p>Locations matching \"";
    pageText += locq;
    pageText += "\":</p>";
    Dstr temp;
    queryResult.print (temp, Format::HTML, StationIndex::xttpdStyle);
    pageText += temp;
  }

  checkedWrite (s, pageText.aschar(), pageText.length());
  endPage (s);
}


// 2016-01-10  Rest in peace, gethostbyaddr.
static void logHits (const sockaddr *saddr, acceptarg3_t saddrlen) {
  char name[256], caddr[256];
  void *src = NULL;
  strcpy(caddr, "?");
  switch (saddr->sa_family) {
  case AF_INET:
    src = &(((sockaddr_in *)saddr)->sin_addr);
    break;
  case AF_INET6:
    src = &(((sockaddr_in6 *)saddr)->sin6_addr);
    break;
  }
  if (src)
    (void)inet_ntop(saddr->sa_family, src, caddr, sizeof(caddr));
  if (getnameinfo(saddr, saddrlen, name, sizeof(name), NULL, 0, NI_NAMEREQD))
    strcpy(name, "(?)");
  Dstr msg (caddr);
  msg += "  ";
  msg += name;
  Global::log (msg, LOG_INFO);
}


static void robots (int s) {
  Dstr pageText ("\
HTTP/1.0 200 OK\n\
MIME-version: 1.0\n\
Content-type: text/plain\n\
\n\
User-agent: *\n\
Disallow: /\n");

  checkedWrite (s, pageText.aschar(), pageText.length());
  close (s);
  exit (0);
}


static void handleRequest (const sockaddr *addr,
			   acceptarg3_t addrlen,
			   int s) {
  // It would have been kind of lame to do this in main and make every
  // request get the same random sequence.
  srand (time (NULL));

  char request[bufsize+1];
  Dstr buf, command, filename;

  logHits (addr, addrlen);

  // This will truncate long requests, which is better than they deserve.
  ssize_t len (read (s, request, bufsize));

  // ssize_t probably means "signed size_t."
  if ((long long int)len >= (long long int)bufsize) {
    Global::log ("Request too long", LOG_WARNING);
    tooLongBarf (s);
  }
  request[len] = '\0';
  buf = request;
  if (buf.length() == 0) {
    Global::log ("Empty client request", LOG_NOTICE);
    badRequestBarf (s, "Empty request");
  }

  buf /= command;
  if (command != "GET") {
    Global::log ("Bad client command: ", command, LOG_NOTICE);
    unimplementedBarf (s);
  }

  buf /= filename;

  if (filename == "/")
    rootPage (s);
  else if (filename == "/robots.txt")
    robots (s);
  else if (filename == "/index.html")
    indexPage (s);
  else if (filename == "/tricks.html")
    tricks (s);
  else if (filename %= "/locations/")
    locationPage (s, filename);
  else if (filename %= "/graphs/")
    graphImage (s, filename);
  else if (filename %= "/zones/")
    zones (s, filename);
  else if (filename %= "/calendar/")
    calendar (s, filename);
  else if (filename %= "/query?location=")
    query (s, filename);
  else if (filename == "/favicon.ico")
    favicon (s);
  else {
    Global::log ("Client tried to get ", filename, LOG_NOTICE);
    notFoundBarf (s);
  }

  exit (0);
}


static void dontBeRoot() {
  errno = 0;
  group *gr = getgrnam (xttpd_group);
  if (!gr) {
    Global::xperror ("getgrnam");
    Dstr msg ("fatal: can't get info on group ");
    msg += xttpd_group;
    Global::log (msg, LOG_ERR);
    exit (-1);
  }
  if (setgid (gr->gr_gid)) {
    Global::xperror ("setgid");
    Dstr msg ("fatal: can't set group to ");
    msg += xttpd_group;
    Global::log (msg, LOG_ERR);
    exit (-1);
  }
  errno = 0;
  passwd *nb = getpwnam (xttpd_user);
  if (!nb) {
    Global::xperror ("getpwnam");
    Dstr msg ("fatal: can't get info on user ");
    msg += xttpd_user;
    Global::log (msg, LOG_ERR);
    exit (-1);
  }
  if (setuid (nb->pw_uid)) {
    Global::xperror ("setuid");
    Dstr msg ("fatal: can't set user to ");
    msg += xttpd_user;
    Global::log (msg, LOG_ERR);
    exit (-1);
  }
}


int main (int argc, char **argv) {
  int listenSocket;

  // The port number given to xttpd is not a "proper" command line
  // setting recognized by XTide and doesn't deserve to be, so
  // if we get one we hide it.
  bool portNumberEvade (false);

  #if !XTTPD_NO_DAEMON
    Global::setDaemonMode();
  #endif

  #if systemd
    {
      int sderr = sd_listen_fds(0);
      if (sderr < 0) {
	Dstr details ("sd_listen_fds: ");
	details += strerror (errno);
	details += '.';
	Global::barf (Error::SYSTEMD_NO_SOCKET, details);
      } else if (sderr != 1)
	Global::barf (Error::SYSTEMD_NO_SOCKET, "Wrong number of file descriptors received from systemd.");
    }
    listenSocket = SD_LISTEN_FDS_START;
  #else
    #if !XTTPD_NO_DAEMON
      if (daemon(0,0) == -1) {
        Global::xperror ("daemon");
        exit (-1);
      }
    #endif

    // Bind the port and then drop root immediately.
    {
      sockaddr_storage addr;
      if (argc >= 2)
	portNumberEvade = parseAddress (addr, argv[1]);
      if (!portNumberEvade) {
	// Default address and port
	memset (&addr, 0, sizeof(addr));
	sockaddr_in *sin = (sockaddr_in *)&addr;
	sin->sin_family      = AF_INET;
	sin->sin_port        = htons(80);
	sin->sin_addr.s_addr = INADDR_ANY;
      }
      setupSocket (addr, listenSocket);
    }
  #endif
  dontBeRoot();
  setWebmaster();

  // An unfortunate consequence of needing to drop root ASAP is that
  // failure to bind the port prevents xttpd -v from working.
  Global::settings.applyUserDefaults();
  Global::settings.applyCommandLine (
      portNumberEvade ? argc-1 : argc,
      portNumberEvade ? &(argv[1]) : argv);
  Global::settings.fixUpDeprecatedSettings();

  // Ignore the locale and serve all web pages in ISO 8859-1.
  Global::codeset = "ISO-8859-1";

  // This should never be used (Visual Studio 2012 can't build xttpd).
  #ifdef NO_INITIALIZER_LISTS
    Global::initializeGraphFont();
  #endif

  // Build the indices.
  zoneIndex.add (Global::stationIndex());
  // Avoid issuing the zoneinfo warning a million times.
  {
    Timestamp nowarn ((time_t)time(NULL));
    zoneinfoIsNotHorriblyObsolete = nowarn.zoneinfoIsNotHorriblyObsolete();
  }
  Global::log ("Accepting connections.", LOG_NOTICE);
  #if systemd
    sd_notify(0, "READY=1");
  #endif

  fd_set rdset;
  FD_ZERO (&rdset);
  FD_SET (listenSocket, &rdset);
  sockaddr_storage addr;
  sockaddr *addrp = (sockaddr *)&addr;
  while (true) {
    fd_set trdset, twrset, texset;
    trdset = rdset;
    // I seem to remember that some platforms barf if you provide
    // null pointers for wrset and exset; best to be safe.
    FD_ZERO (&twrset);
    FD_ZERO (&texset);
    int ts (-1);
    ts = select (listenSocket+1, &trdset, &twrset, &texset, NULL);
    if (ts > 0) {
      // acceptarg3_t is set by configure, but now that we are relying on
      // new-age net functions, it pretty much has to be socklen_t.
      acceptarg3_t length (sizeof(addr));
      int newSocket;
      if ((newSocket = accept (listenSocket, addrp, &length)) != -1) {
	assert (length <= sizeof(addr));
        if (!fork())
          handleRequest (addrp, length, newSocket);
        close (newSocket);
      }
    }
    // Clean up zombies.
    while (waitpid (-1, (int*)NULL, WNOHANG|WUNTRACED) > 0);
  }

  exit (0);
}

// This messy interface was developed as a proof of concept in hope
// that the XTide community would develop a better one.  But the XTide
// community never showed much interest, preferring instead to use CGI
// and PHP.  The only significant barrier to using CGI or PHP in the
// first place was the startup overhead of XTide, and that was reduced
// to tolerable levels by libtcd.  Thus there is little incentive to
// improve this code, and in the event that serious problems are found
// with it (like security vulnerabilities), the most likely treatment
// will be euthanasia.
