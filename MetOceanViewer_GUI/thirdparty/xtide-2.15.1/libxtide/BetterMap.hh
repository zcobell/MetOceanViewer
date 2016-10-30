/*  $Id: BetterMap.hh 5748 2014-10-11 19:38:53Z flaterco $

    BetterMap:  std::map plus a const operator[].

    Copyright (C) 2006  David Flater.

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

// Putting the bodies of the methods in a separate file did not work
// with g++ 3.4.6 / GNU ld version 2.15.92.0.2 20040927.

#include <map>

namespace libxtide {

template <typename KeyT, typename ElmT> class BetterMap:
public std::map<KeyT,ElmT> {

public:

  // Allow regular non-const operator[] to continue working.
  using std::map<KeyT,ElmT>::operator[];

  const ElmT &operator[] (const KeyT &k) const {
    typename std::map<KeyT,ElmT>::const_iterator it (find(k));
    assert (it != this->end());
    return it->second;
  }

  // The STL included with g++ 4.1.0 provides an at() method in both
  // const and non-const versions that throws std::out_of_range if the
  // key is not matched.  However, at() is not in ISO/IEC 14882:2003
  // and it's not in the STL provided with g++ 3.4.6.

};

}
