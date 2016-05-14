// $Id: Units.cc 5748 2014-10-11 19:38:53Z flaterco $
/*
    Units

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
namespace libxtide {


static const unsigned numUnits = 4;
static constString longNames[numUnits] = {"feet",
                                          "meters",
                                          "knots",
                                          "knots^2"};
static constString shortNames[numUnits] = {"ft",
                                           "m",
                                           "kt",
                                           "kt^2"};


constString Units::shortName (PredictionUnits u) {
  assert (u >= 0 && u < (int)numUnits);
  return shortNames[u];
}


constString Units::longName (PredictionUnits u) {
  assert (u >= 0 && u < (int)numUnits);
  return longNames[u];
}


const Units::PredictionUnits Units::parse (const Dstr &unitsName) {
  for (unsigned i=0; i<numUnits; ++i)
    if (unitsName == longNames[i] || unitsName == shortNames[i])
      return (PredictionUnits)i;
  Dstr details ("The offending units were ");
  details += unitsName;
  details += '.';
  Global::barf (Error::UNRECOGNIZED_UNITS, details);
  return meters; // Unreachable statement to silence compiler warning
}


const bool Units::isCurrent (PredictionUnits u) {
  assert (u != zulu);
  return (u == knots || u == knotsSquared);
}


const bool Units::isHydraulicCurrent (PredictionUnits u) {
  assert (u != zulu);
  return (u == knotsSquared);
}


const Units::PredictionUnits Units::flatten (PredictionUnits u) {
  assert (u != zulu);
  if (u == knotsSquared)
    return knots;
  return u;
}

}
