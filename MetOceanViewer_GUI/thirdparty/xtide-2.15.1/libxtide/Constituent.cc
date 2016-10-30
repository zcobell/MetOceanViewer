// $Id: Constituent.cc 5748 2014-10-11 19:38:53Z flaterco $

// Constituent:  All that which pertains to a specific constituent
// from a station viewpoint:  speed, equilibrium arguments, node
// factors, amplitude, and phase.

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

#include "libxtide.hh"
namespace libxtide {


void Constituent::checkValid (Year year) const {
  if (year < _firstValidYear || year > _lastValidYear) {
    Dstr details ("The years supported by the harmonics file are ");
    details += _firstValidYear.val();
    details += " through ";
    details += _lastValidYear.val();
    details += ".\n";
    details += "The offending year was ";
    details += year.val();
    details += ".\n";
    Global::barf (Error::YEAR_NOT_IN_TABLE, details);
  }
}


const Year Constituent::firstValidYear() const {
  return _firstValidYear;
}


const Year Constituent::lastValidYear() const {
  return _lastValidYear;
}


const Angle Constituent::arg (Year year) const {
  checkValid (year);
  return args[year.val()-_firstValidYear.val()];
}


const double Constituent::nod (Year year) const {
  checkValid (year);
  return nods[year.val()-_firstValidYear.val()];
}


Constituent::Constituent (double speed_degreesPerHour,
                          int32_t startYear,
                          uint32_t numberOfYears,
                          const float *args_degrees,
                          const float *nodes,
                          Amplitude amplitude_,
                          float phase_degrees):
  speed(speed_degreesPerHour),
  amplitude(amplitude_),
  phase(Units::degrees, -phase_degrees),
  args(numberOfYears),
  nods(numberOfYears),
  _firstValidYear(startYear),
  _lastValidYear(startYear+numberOfYears-1) {
  assert (_lastValidYear >= _firstValidYear);
  for (unsigned looper=0; looper<numberOfYears; ++looper) {
    args[looper] = Angle (Units::degrees, args_degrees[looper]);
    nods[looper] = nodes[looper];
  }
}

}
