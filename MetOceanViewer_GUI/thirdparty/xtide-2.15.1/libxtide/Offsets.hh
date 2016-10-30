// $Id: Offsets.hh 5748 2014-10-11 19:38:53Z flaterco $

// Offsets:  storage for tide offsets.

/*
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

class SimpleOffsets {
public:

  SimpleOffsets();  // default to 0 s, 0 Zulu, 1.0

  // levelMultiply = 0 is treated as null and defaulted to 1.0.
  // levelMultiply < 0 is an error.
  SimpleOffsets (Interval        timeAdd,
                 PredictionValue levelAdd,
                 double          levelMultiply);

  const Interval        timeAdd()       const;
  const PredictionValue levelAdd()      const;
  const double          levelMultiply() const;

protected:
  Interval        _timeAdd;
  PredictionValue _levelAdd;
  double          _levelMultiply;
};


class HairyOffsets {
public:

  HairyOffsets (const SimpleOffsets &maxso,
                const SimpleOffsets &minso,
                NullableInterval floodBegins,
                NullableInterval ebbBegins);

  const Interval         maxTimeAdd()       const;
  const PredictionValue  maxLevelAdd()      const;
  const double           maxLevelMultiply() const;
  const Interval         minTimeAdd()       const;
  const PredictionValue  minLevelAdd()      const;
  const double           minLevelMultiply() const;
  const NullableInterval floodBegins()      const;
  const NullableInterval ebbBegins()        const;

  // Try to reduce to SimpleOffsets.
  // If possible, returns true and simpleOffsets_out is valid.
  // If not, returns false and simpleOffsets_out is unchanged.
  // Note special case for hydraulic currents in HarmonicsFile.cc.
  const bool trySimplify (SimpleOffsets &simpleOffsets_out) const;

protected:
  SimpleOffsets _maxso, _minso;
  NullableInterval _floodBegins, _ebbBegins;
};

const bool operator== (const SimpleOffsets &a, const SimpleOffsets &b);
const bool operator!= (const SimpleOffsets &a, const SimpleOffsets &b);

}
