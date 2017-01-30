// $Id: StationIndex.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  StationIndex  Collection of StationRefs.

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
#include "HarmonicsFile.hh"

namespace libxtide {


// Maximum length of HTML tables (Netscape has trouble with really big
// tables).
static const unsigned maxHTMLtableLength = 100U;


char **StationIndex::makeStringList (unsigned long startAt,
                                     unsigned long maxLength) const {
  if (!size()) {
    char **temp = (char **) malloc (sizeof (char *));
    temp[0] = NULL;
    return temp;
  }

  assert (startAt < size());
  if (size() - startAt < maxLength)
    maxLength = size() - startAt;
  char **temp = (char **) malloc ((maxLength+1) * sizeof (char *));

  for (unsigned long i=startAt; i<startAt+maxLength; ++i) {
    Dstr styp;

    if (operator[](i)->isReferenceStation)
      styp = "Ref";
    else
      styp = "Sub";

    char cbuf[20];
    char tempbuf[121];
    if (operator[](i)->coordinates.isNull())
      sprintf (cbuf, "       NULL       ");
    else
      sprintf (cbuf, "%8.4f %9.4f",
                             operator[](i)->coordinates.lat(),
                             operator[](i)->coordinates.lng());
    sprintf (tempbuf, "%-80.80s %-4.4s %18.18s",
                             operator[](i)->name.aschar(),
                             styp.aschar(),
                             cbuf);
    temp[i-startAt] = strdup (tempbuf);
  }
  temp[maxLength] = NULL;
  return temp;
}


const double StationIndex::bestCenterLongitude() const {
  // -180 -150 -120 -90 -60 -30 0 30 60 90 120 150
  unsigned long counters[12] = {0,0,0,0,0,0,0,0,0,0,0,0};
  unsigned long i;
  for (i=0; i<size(); ++i) {
    const Coordinates &c = operator[](i)->coordinates;
    if (!(c.isNull())) {
      int j (Global::iround((c.lng()+180.0)/30.0));
      if (j == 12)
        j = 0;
      assert (j >= 0 && j < 12);
      ++(counters[j]);
    }
  }
  unsigned best = 0;
  for (i=1; i<12; ++i) {
    if (counters[i] > counters[best])
      best = i;
  }
  return (double)best * 30.0 - 180.0;
}


void StationIndex::addHarmonicsFile (const Dstr &harmonicsFileName) {
  Dstr msg ("Indexing ");
  msg += harmonicsFileName;
  msg += "...";
  Global::log (msg, LOG_NOTICE);

  // Every StationRef from this HarmonicsFile gets a reference to this
  // string, so it has to be persistent.
  HarmonicsFile h (*(new Dstr (harmonicsFileName)));

  StationRef *s;
  while ((s = h.getNextStationRef()))
    push_back (s);

  if (!(_hfileIDs.isNull()))
    _hfileIDs += "<br>";
  _hfileIDs += h.versionString();
}


void StationIndex::hfileIDs (Dstr &hfileIDs_out) {
  hfileIDs_out = _hfileIDs;
}


void StationIndex::sort (SortKey sortKey) {
  switch (sortKey) {
  case StationIndex::sortByName:
    std::sort (begin(), end(), libxtide::sortByName);
    break;
  case StationIndex::sortByLat:
    std::sort (begin(), end(), libxtide::sortByLat);
    break;
  case StationIndex::sortByLng:
    std::sort (begin(), end(), libxtide::sortByLng);
    break;
  default:
    assert (false);
  }
}


StationRef * const StationIndex::getStationRefByLatin1Name (
						      const Dstr &name) const {
  for (unsigned long i=0; i<size(); ++i)
    if (operator[](i)->name %= name)
      return operator[](i);
  return NULL;
}


StationRef * const StationIndex::getStationRefByName (const Dstr &name) const {
  static bool warned = false;
  StationRef *sr = NULL;
  Dstr correct (name), incorrect (name);
  if (Global::codeset == "UTF-8")
    correct.unutf8();
#ifdef DSTR_MAJOR_REV
  else if (Global::codeset == "CP437")
    correct.unCP437();
#endif
  else
    incorrect.unutf8();
  if (!correct.isNull())
    sr = getStationRefByLatin1Name (correct);
  if (!sr && !incorrect.isNull()) {
    sr = getStationRefByLatin1Name (incorrect);
    if (sr && !warned) {
      warned = true;
      Global::log ("Warning: codeset of location name appears to disagree with ambient locale", LOG_WARNING);
    }
  }
  return sr;
}


static void startLocListHTML (Dstr &d) {
  d += "<p><table>\n<tr><th>Location</th><th>Type</th>\n\
<th>Coordinates</th></tr>";
}


static void endLocListHTML (Dstr &d) {
  d += "</table></p>\n";
}


static void listLocationHTML (Dstr &d,
                              const StationRef *sr,
			      StationIndex::WebListStyle style) {
  assert (sr);
  d += "<tr><td>";
  if (style == StationIndex::xttpdStyle) {
    d += "<a href=\"/locations/";
    d += sr->rootStationIndexIndex;
    d += ".html\">";
  }
  d += sr->name;
  if (style == StationIndex::xttpdStyle)
    d += "</a>";
  d += "</td><td>";
  if (sr->isReferenceStation)
    d += "Ref";
  else
    d += "Sub";
  d += "</td><td>";
  Dstr tempc;
  sr->coordinates.print (tempc);
  d += tempc;
  d += "</td></tr>\n";
}


void StationIndex::print (Dstr &text_out,
                          Format::Format form,
			  WebListStyle style) const {
  switch (form) {
  case Format::HTML:
    {
      text_out = "";
      if (style != xttpdStyle) {
        text_out += "<p>Location list generated ";
        Timestamp now ((time_t)(time(NULL)));
        Dstr tempnow;
        now.print (tempnow, "UTC0");
        text_out += tempnow;
        text_out += "</p>\n\n";
      }
      startLocListHTML (text_out);
      for (unsigned long i=0; i<size(); ++i) {
	listLocationHTML (text_out, operator[](i), style);
	if ((i % maxHTMLtableLength == 0) && (i != 0)) {
	  endLocListHTML (text_out);
	  startLocListHTML (text_out);
	}
      }
      endLocListHTML (text_out);
    }
    break;
  case Format::text:
    {
      unsigned tw = Global::settings["tw"].u;
      text_out = "Location list generated ";
      Timestamp now ((time_t)(time(NULL)));
      Dstr tempnow;
      now.print (tempnow, "UTC0");
      text_out += tempnow;
      text_out += "\n\n";
      int namewidth = (int)tw - 28;
      if (namewidth < 10)
	namewidth = 10;
      char fmt[80];
      sprintf (fmt, "%%-%d.%ds %%-3.3s %%s\n", namewidth, namewidth);
      SafeVector<char> buf (tw + 30);
      for (unsigned long i=0; i<size(); ++i) {
	Dstr styp, c;
	if (operator[](i)->isReferenceStation)
	  styp = "Ref";
	else
	  styp = "Sub";
	operator[](i)->coordinates.print (c, Coordinates::fixedWidth);
        if (Global::needDegrees())
          c.repstr ("°", Global::degreeSign);
	sprintf (&buf[0],
                 fmt,
                 operator[](i)->name.aschar(),
                 styp.aschar(),
                 c.aschar());
	text_out += &buf[0];
      }
    }
    break;
  default:
    Global::formatBarf (Mode::list, form);
  }
  Global::finalizeCodeset (text_out, Global::codeset, form);
}


void StationIndex::query (const Dstr &pattern,
                          StationIndex &index,
                          QueryType queryType) const {
  for (unsigned long i=0; i<size(); ++i) {
    StationRef *sr = operator[](i);
    bool patternMatch;
    switch (queryType) {
    case percentEqual:
      patternMatch = (sr->name %= pattern);
      break;
    case contains:
      patternMatch = (sr->name.contains (pattern));
      break;
    default:
      assert (false);
    }
    if (patternMatch)
      index.push_back (sr);
  }
}


void StationIndex::setRootStationIndexIndices() {
  for (unsigned long i=0; i<size(); ++i)
    operator[](i)->rootStationIndexIndex = i;
}

}
