// $Id: ZoneIndex.cc 5749 2014-10-11 19:42:10Z flaterco $	

/*  ZoneIndex  Index stations by zone for xttpd.

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
#include "ZoneIndex.hh"


ZoneIndex::ZInode * const ZoneIndex::ZImap::lookup (const Dstr &zone) {
  assert (zone.length());

  ZImap::iterator xactmatch = find (zone);
  if (xactmatch != end())
    return &(xactmatch->second);

  for (ZImap::iterator it = begin(); it != end(); ++it)
    // ":America/New_York" %= ":America/"
    if ((zone %= it->first) && (it->first.back() == '/'))
      return it->second.subzones.lookup (zone);

  return NULL;
}


ZoneIndex::ZInode * const ZoneIndex::operator[] (const Dstr &zone) {
  if (zone.length())
    return top.subzones.lookup (zone);
  return &top;
}


ZoneIndex::ZInode &ZoneIndex::makezone (const Dstr &zone) {
  assert (zone.length());
  assert (zone.back() != '/');
  ZInode *tryit = operator[](zone);
  if (tryit)
    return *tryit;
  else {
    Dstr smallzone (zone), addzone (zone);
    int i;
    // Find the level that already exists.
    while ((i = smallzone.strrchr ('/')) != -1) {
      if (i == (int)smallzone.length() - 1) {
        addzone = smallzone;
        smallzone -= i;
      } else {
        smallzone -= i+1;
        if ((tryit = operator[](smallzone)))
          break;
      }
    }
    // Add one more level.
    if (tryit)
      (void) tryit->subzones[addzone];
    else
      (void) top.subzones[addzone];
    // Repeat till done.
    return makezone (zone);
  }
}


void ZoneIndex::add (StationRef *sr) {
  assert (sr);
  const Dstr &zone = sr->timezone;
  assert (!zone.isNull());
  makezone(zone).stationIndex.push_back (sr);
}


void ZoneIndex::add (const StationIndex &stationIndex) {
  Global::log ("Building zone index...", LOG_NOTICE);
  for (unsigned long i=0; i<stationIndex.size(); ++i)
    add (stationIndex[i]);
}

// Cleanup2006 Cruft CloseEnough
