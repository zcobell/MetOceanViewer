// $Id: Constituent.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {

class Constituent {
public:

  // Data types accepted by the constructor are as provided by libtcd,
  // except for amplitude.  in_phase is negated on input to produce -k'.
  Constituent (double speed_degreesPerHour,
               int32_t startYear,
	       uint32_t numberOfYears,
               const float *args_degrees,
               const float *nodes,
               Amplitude amplitude_,
               float phase_degrees);

  // Speed never changes.  Amplitude and phase might be modified by
  // ConstituentSet's constructor to apply adjustments.

  Speed speed;
  const Angle  arg (Year year) const;   // Equilibrium arguments
  const double nod (Year year) const;   // Node factors
  Amplitude amplitude;
  Angle phase;                          // A.k.a. epoch, -k'

  const Year firstValidYear() const;
  const Year lastValidYear()  const;

protected:

  SafeVector<Angle>  args;
  SafeVector<double> nods;
  Year _firstValidYear, _lastValidYear;

  void checkValid (Year year) const;
};

}
