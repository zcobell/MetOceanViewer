// $Id: StationIndex.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {


// "STL containers are not intended to be used as base classes (their
// destructors are deliberately non-virtual).  Deriving from a
// container is a common mistake made by novices."
// -- Standard Template Library,
// http://en.wikipedia.org/w/index.php?title=Standard_Template_Library&oldid=98705028
// (last visited January 13, 2007).

// It's not const StationRef* because setRootStationIndexIndices() has
// to work.
class StationIndex: public SafeVector<StationRef*> {
public:

  // Import refs for all stations in the specified harmonics file.
  void addHarmonicsFile (const Dstr &harmonicsFileName);

  // This is a front end to std::sort that uses the comparison
  // functions declared in StationRef.hh.
  enum SortKey {sortByName, sortByLat, sortByLng};
  void sort (SortKey sortKey = StationIndex::sortByName);

  // QueryType refers to the applicable Dstr operation, %= or
  // contains.  Selected station refs are added to index (any refs
  // already in there are unchanged).
  enum QueryType {percentEqual, contains};
  void query (const Dstr &pattern,
              StationIndex &index,
              QueryType queryType) const;

  // This implements list mode.
  enum WebListStyle {xttpdStyle, normalStyle};
  void print (Dstr &text_out,
              Format::Format form,
              WebListStyle style = normalStyle) const;

  // Construct array of character strings used by xxLocationList.
  char **makeStringList (unsigned long startAt,
                         unsigned long maxLength) const;

  // Return identifiers for imported harmonics files in HTML format.
  void hfileIDs (Dstr &hfileIDs_out);

  // Find a station by %= name, with UTF-8 / Latin-1 conversion as
  // needed.  Return NULL if not found.
  StationRef * const getStationRefByName (const Dstr &name) const;

  // Return longitude with the most stations.
  const double bestCenterLongitude() const;

  // xttpd needs StationRef::rootStationIndexIndex to be set on all
  // station refs.  This method sets those fields in all station refs
  // in the current index.  It should be called on the root
  // StationIndex after all harmonics files have been indexed.
  void setRootStationIndexIndices();

protected:
  Dstr _hfileIDs;

  // Without locale correction.  Return NULL if not found.
  StationRef * const getStationRefByLatin1Name (const Dstr &name) const;
};

}
