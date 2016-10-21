/*  $Id: ZoneIndex.hh 2641 2007-09-02 21:31:02Z flaterco $

    ZoneIndex  Index stations by zone for xttpd.

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

// This crufty class is only used in xttpd and will hopefully be
// obsoleted by a better location chooser in xttpd someday.

class ZoneIndex {
public:
  struct ZInode;

  // "STL containers are not intended to be used as base classes (their
  // destructors are deliberately non-virtual).  Deriving from a
  // container is a common mistake made by novices."
  // -- Standard Template Library,
  // http://en.wikipedia.org/w/index.php?title=Standard_Template_Library&oldid=98705028
  // (last visited January 13, 2007).

  // Time zones are assembled in a tree structure.  BetterMap is used
  // primarily because it keeps the nodes sorted by zone name.
  // BetterMap::operator[] is not useful outside of ZoneIndex; clients
  // should use ZoneIndex::operator[] instead.  (Bad, bad; should hide
  // the unwanted operator.)

  // map key is name of time zone.  For leaves it is the entire name;
  // for others it is like ":America/".  In practice it cannot happen
  // that a given name would have both stations and subzones, but if
  // it did happen, the stations would go on a node called
  // ":Something" while the subzones would go on a node called
  // ":Something/" (with trailing slash).

  class ZImap: public BetterMap<const Dstr, ZInode> {
  public:
    // lookup delves into subzones if needed, returns NULL if not found.
    ZInode * const lookup (const Dstr &zone);
  };

  struct ZInode {
    StationIndex stationIndex; // Stations with this time zone
    ZImap subzones;
  };

  // Look up a time zone, leaf or non-leaf.  Look up "" to retrieve
  // the top level.  Returns NULL if not found.
  ZInode * const operator[] (const Dstr &zone);

  // Add every StationRef in the index.
  void add (const StationIndex &stationIndex);

protected:
  ZInode top;

  // lookup or create the zone.
  ZInode &makezone (const Dstr &zone);

  // Add one StationRef.
  void add (StationRef *sr);
};

// Cleanup2006 Cruft CloseEnough
