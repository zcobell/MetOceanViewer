// $Id: TideEventsOrganizer.hh 5748 2014-10-11 19:38:53Z flaterco $

// TideEventsOrganizer  Collect, sort, subset, and iterate over tide events.

/*
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

typedef std::multimap<const Timestamp, TideEvent>
                                   TideEventsMap;
typedef std::multimap<const Timestamp, TideEvent>::iterator
                                   TideEventsIterator;
typedef std::multimap<const Timestamp, TideEvent>::reverse_iterator
                                   TideEventsReverseIterator;

// "STL containers are not intended to be used as base classes (their
// destructors are deliberately non-virtual).  Deriving from a
// container is a common mistake made by novices."
// -- Standard Template Library,
// http://en.wikipedia.org/w/index.php?title=Standard_Template_Library&oldid=98705028
// (last visited January 13, 2007).

class TideEventsOrganizer: public TideEventsMap {
public:

  // Add a TideEvent, making an effort to suppress duplicates.  An
  // event is considered a duplicate if the difference in times is
  // less than Global::eventSafetyMargin and the event type is the
  // same.  This heuristic is almost always correct, and when it is
  // wrong, the affected events are probably anomalous to begin with.

  // The test for duplicates does not satisfy the mathematical
  // definition of an equivalence relation (it is not transitive).
  // Consequently, the duplicate elimination cannot be delegated to
  // the STL.  Also, inserting the same set of events in a different
  // order can get different results.

  void add (const TideEvent &tideEvent);
};

}
