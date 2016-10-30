/*  $Id: SafeVector.hh 5748 2014-10-11 19:38:53Z flaterco $

    SafeVector equals std::vector except with bounds checking on
    operator[].

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

#include <vector>

namespace libxtide {


template <class T> class SafeVector: public std::vector<T> {
public:

  // Allow relevant constructors to continue working.  (Not sure why
  // these definitions are needed here when no definition of the
  // default constructor is needed in BetterMap--one of the deep
  // mysteries of templates I guess.)
  SafeVector (): std::vector<T>() {}
  SafeVector (unsigned n): std::vector<T>(n) {}
  #ifndef NO_VECTOR_INITLISTS
  SafeVector (std::initializer_list<T> l): std::vector<T>(l) {}
  #endif

  // Die with assertion failure if index >= size().
  T &operator[] (unsigned index) {
    assert (index < this->size());
    return std::vector<T>::operator[] (index);
  }

  // Die with assertion failure if index >= size().
  const T &operator[] (unsigned index) const {
    assert (index < this->size());
    return std::vector<T>::operator[] (index);
  }

  // The at() method throws std::out_of_range if index >= size().  It
  // does not appear in the venerable and freely available SGI
  // Standard Template Library Programmer's Guide (1994?), but it's
  // there in ISO/IEC 14882:1998, supplementing rather than replacing
  // the dangerous unchecked operator[].

  // IMO there is no good reason for operator[] to be unsafe and thus
  // no reason for at().  If you want C, you know where to find it.

};

}
