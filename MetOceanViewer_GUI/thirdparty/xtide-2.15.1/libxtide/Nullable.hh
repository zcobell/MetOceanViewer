// $Id: Nullable.hh 5748 2014-10-11 19:38:53Z flaterco $

// Nullable:  mix-in to make a data type nullable.

/*
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


// Lecture on inheritance, Part 2.  (For Part 1, see Amplitude.hh.)

// There are two ways to implement nullability, only one of which
// results in valid inheritance if you make a nullable subclass of a
// non-nullable data type like this:
//   class PlainOldNumber { ... }
//   class NullableNumber: public PlainOldNumber, public Nullable { ... }

// The "soft" or "lax" implementation enables clients to check for
// nullness, but if they don't, nothing prevents them from using and
// propagating garbage values of the underlying data type.  While the
// absence of integrity checking is disturbing, the resulting
// subclasses preserve substitutability.

// The "hard" or "strict" implementation prevents clients from using
// or propagating garbage values.  This breaks substitutability
// because an operation that succeeds on every value in the domain of
// the underlying data type may fail on a null value.  If the
// operation was written for the underlying data type, it won't even
// know to check for null.

// The hard implementation is not a problem if it is used like this
// with no non-nullable parent class:
//   class NullableNumber: public Nullable { ... }

namespace libxtide {

class Nullable {
public:

  // No reason to use Nullable alone, hence no public constructor.

  // Since there is little chance that I would ever delete a Nullable
  // (as opposed to the correct subclass), it is not worth the
  // performance hit to force Timestamp to carry around virtual
  // function overhead.
  // virtual ~Nullable();

  const bool isNull () const;
  void makeNull ();

protected:
  Nullable ();               // Makes a null.
  Nullable (bool isNull);    // Init to null if true.
  bool _isNull;
};

}
