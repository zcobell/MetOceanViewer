/*  $Id: ConstituentSet.hh 5748 2014-10-11 19:38:53Z flaterco $

    ConstituentSet:  set of constituents, datum, and related methods.

    Copyright (C) 1997  David Flater.

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

class ConstituentSet {
public:

  // Null constituents should have been eliminated in HarmonicsFile.
  // On construction, constituents and datum are (permanently)
  // adjusted according to adjustments.
  ConstituentSet (const SafeVector<Constituent> &constituents,
                  PredictionValue datum,
                  const SimpleOffsets &adjustments);

  // Change preferred units of length.
  // Default is as specified by settings.
  // Attempts to set same units are tolerated without complaint.
  // Attempts to set velocity units are punished.
  void setUnits (Units::PredictionUnits units);

  // Tell me what units tideDerivative will return.
  const Units::PredictionUnits predictUnits () const;

  // These will never have a value of type KnotsSquared.
  // maxAmplitudeHeuristic is a practical estimate of the maximum amplitude
  // that will be generated.  Occasionally it may be exceeded, but the true
  // mathematical bound is usually much too conservative for determining the
  // range of tide graphs.
  const Amplitude       maxAmplitudeHeuristic() const;
  const PredictionValue datum()                 const;

  // Calculate (deriv)th time derivative of the normalized tide (for
  // time in s).  The result does not have the datum added in and will
  // not be converted from KnotsSquared.
  const PredictionValue tideDerivative (Timestamp predictTime, unsigned deriv);

#ifdef blendingTest
  // For testing only.
  void tideDerivativeBlendValues (Timestamp predictTime,
				  unsigned deriv,
				  NullablePredictionValue &firstYear_out,
				  NullablePredictionValue &secondYear_out);
#endif

  // Return the maximum that the absolute value of the (deriv)th
  // derivative of the tide can ever attain, plus "a little safety
  // margin."  tideDerivativeMax(0) == maxAmplitude() * 1.1
  const Amplitude tideDerivativeMax (unsigned deriv) const;

protected:

  SafeVector<Constituent> _constituents;
  const unsigned length; // = _constituents.size()
  PredictionValue _datum;

  // The following are what get accessed directly in tideDerivative.
  // Amplitudes are constituent amplitudes times node factors.
  // Phases are constituent phases plus equilibrium arguments.

  // Optimization:  Conversion of amplitudes to PredictionValue is
  // done in advance instead of on every reference inside of
  // tideDerivative.  The conversion from Amplitude to PredictionValue
  // could not be inlined because of a circular dependency.
  SafeVector<PredictionValue> amplitudes;
  SafeVector<Angle>           phases;

  // Maximum derivative supported by tideDerivative and family.
  static const unsigned maxDeriv = 2U;

  Amplitude _maxAmplitudeHeuristic;
  Amplitude maxdt[maxDeriv+2];
  Year      currentYear;
  Timestamp epoch;
  Timestamp nextEpoch;
  Units::PredictionUnits preferredLengthUnits;

  // Update amplitudes, phases, epoch, nextEpoch, and currentYear.
  void changeYear (Year newYear);

  // Calculate (deriv)th time derivative of the normalized tide for
  // time in s since the beginning (UTC) of currentYear, WITHOUT
  // changing years or blending.
  const PredictionValue tideDerivative (Interval sinceEpoch, unsigned deriv);

  // Called by tideDerivative(Timestamp) to blend tides near year ends.
  const PredictionValue blendTide (Timestamp predictTime,
				   unsigned deriv,
				   Year firstYear,
				   double blend);
};

}
