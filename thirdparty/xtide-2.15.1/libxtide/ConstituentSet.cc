// $Id: ConstituentSet.cc 5748 2014-10-11 19:38:53Z flaterco $
/*
    ConstituentSet:  set of constituents, datum, and related methods.

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


/* tideBlendInterval
 *   Half the number of seconds over which to blend the tides from
 *   one epoch to the next.
 */
static const Interval tideBlendInterval (3600U);

// Number of constituents to use for the heuristic estimate of maximum
// amplitude.
static const unsigned numConstForAmplitude (6U);


// Convert to preferredLengthUnits if this conversion makes sense;
// return value unchanged otherwise.

static const Amplitude prefer (Amplitude v,
			       Units::PredictionUnits preferredLengthUnits) {
  assert (!Units::isCurrent(preferredLengthUnits));
  if (!Units::isCurrent(v.Units()) && v.Units() != preferredLengthUnits)
    v.Units (preferredLengthUnits);
  return v;
}


static const PredictionValue prefer (
                                 PredictionValue v,
				 Units::PredictionUnits preferredLengthUnits) {
  assert (!Units::isCurrent(preferredLengthUnits));
  if (!Units::isCurrent(v.Units()) && v.Units() != preferredLengthUnits)
    v.Units (preferredLengthUnits);
  return v;
}


ConstituentSet::ConstituentSet (const SafeVector<Constituent> &constituents,
                                PredictionValue datum,
                                const SimpleOffsets &adjustments):
  _constituents(constituents),
  length(constituents.size()),
  _datum(datum),
  currentYear(2000),
  preferredLengthUnits(Units::meters) {
  unsigned i;

  if (!Units::isCurrent(_datum.Units()))
    preferredLengthUnits = _datum.Units(); // Native units of station

  // Null constituents should have been eliminated in HarmonicsFile.

  // Apply adjustments.
  _datum *= adjustments.levelMultiply();
  _datum.convertAndAdd (adjustments.levelAdd());
  for (i=0;i<length;++i) {
    _constituents[i].amplitude *= adjustments.levelMultiply();
    // To move tides one hour later, you need to turn BACK the phases.
    _constituents[i].phase -= adjustments.timeAdd() * _constituents[i].speed;
  }

  {
    // Temporarily cache amplitudes times node factors for all years for use in
    // figuring maxdt and estimating max amplitude.  It is not necessary to
    // remember which year is which.
    const unsigned numYears (_constituents[0].lastValidYear().val() -
			     _constituents[0].firstValidYear().val() + 1);
    SafeVector<SafeVector<Amplitude> > allAmps (numYears);
    {
      Year loopyear = _constituents[0].firstValidYear();
      for (SafeVector<SafeVector<Amplitude> >::iterator it = allAmps.begin();
	   it != allAmps.end();
	   ++it, ++loopyear) {
	SafeVector<Amplitude> &yearAmps (*it);
	yearAmps.resize (length);
	for (i=0;i<length;++i)
	  yearAmps[i] = _constituents[i].amplitude
		      * _constituents[i].nod(loopyear);
      }
    }

    // Figure maxdt.
    for (unsigned deriv=0; deriv<=maxDeriv+1; ++deriv) {
      for (SafeVector<SafeVector<Amplitude> >::iterator it = allAmps.begin();
	   it != allAmps.end();
	   ++it) {
	SafeVector<Amplitude> &yearAmps (*it);
	Amplitude max;
	for (i=0;i<length;++i)
	  max += yearAmps[i] * pow(_constituents[i].speed.radiansPerSecond(),
				   (double)deriv);
	if (max > maxdt[deriv])
	  maxdt[deriv] = max;
      }
      maxdt[deriv] *= 1.1;      /* Add a little safety margin... */
    }

    // Estimate max amplitude from numConstForAmplitude biggest constituents.
    // Destructive to allAmps.
    for (SafeVector<SafeVector<Amplitude> >::iterator it = allAmps.begin();
	 it != allAmps.end();
	 ++it) {
      SafeVector<Amplitude> &yearAmps (*it);
      SafeVector<Amplitude>::iterator middle (yearAmps.begin());
      if (length < numConstForAmplitude)
	middle = yearAmps.end();
      else
	std::advance (middle, numConstForAmplitude);
      std::partial_sort (yearAmps.begin(), middle, yearAmps.end(),
			 std::greater<Amplitude>());
      Amplitude max;
      for (i=0; i<numConstForAmplitude && i<length; ++i)
	max += yearAmps[i];
      if (max > _maxAmplitudeHeuristic)
	_maxAmplitudeHeuristic = max;
    }
  }
  if (Units::isHydraulicCurrent(_maxAmplitudeHeuristic.Units()))
    _maxAmplitudeHeuristic.Units(Units::flatten(_maxAmplitudeHeuristic.Units()));
  assert (_maxAmplitudeHeuristic.val() > 0.0);

  // Harmonics file range of years may exceed that of this platform.
  // Try valiantly to find a safe initial value.
  {
    unsigned b = _constituents[0].firstValidYear().val();
    unsigned e = _constituents[0].lastValidYear().val();
    if (b <= 2000 && e >= 2000)
      currentYear = 2000;
    else if (b <= 1970 && e >= 1970)
      currentYear = 1970;
    else if (b <= 2037 && e >= 2037)
      currentYear = 2037;
    else
      currentYear = (b+e)/2;
  }

  amplitudes.resize (length);
  phases.resize     (length);
  changeYear (currentYear);
}


void ConstituentSet::setUnits (Units::PredictionUnits units) {
  assert (!isCurrent(units));
  preferredLengthUnits = units;
}


const Units::PredictionUnits ConstituentSet::predictUnits () const {
  Units::PredictionUnits temp (_constituents[0].amplitude.Units());
  if (Units::isCurrent(temp))
    return temp;
  return preferredLengthUnits;
}


const Amplitude ConstituentSet::maxAmplitudeHeuristic() const {
  return prefer (_maxAmplitudeHeuristic, preferredLengthUnits);
}


const PredictionValue ConstituentSet::datum() const {
  return prefer (_datum, preferredLengthUnits);
}


const Amplitude ConstituentSet::tideDerivativeMax (unsigned deriv) const {
  /* We need to be able to calculate max tide derivatives for one
   * derivative higher than we actually need to know the tides.
   */
  assert (deriv <= maxDeriv+1);
  // This is initialized in the constructor.
  return prefer (maxdt[deriv], preferredLengthUnits);
}


// Update amplitudes, phases, epoch, nextEpoch, and currentYear.
void ConstituentSet::changeYear (Year newYear) {
  currentYear = newYear;

  for (unsigned i=0; i<length; ++i) {

    // Apply node factor.  (Implicit conversion to PredictionValue.)
    amplitudes[i] = _constituents[i].amplitude *
		    _constituents[i].nod(currentYear);

    // Apply equilibrium argument.  Recall that phases have been pre-negated
    // per -k'.
    phases[i] = _constituents[i].phase + _constituents[i].arg(currentYear);
  }

  epoch     = Timestamp (currentYear);
  nextEpoch = Timestamp (currentYear + 1);

  // nextEpoch is allowed to fail, which allows us to get tides for
  // the first few days of 2038, but epoch we need.
  if (epoch.isNull())
    Global::barf (Error::TIMESTAMP_OVERFLOW);
}


// The following block of functions is slightly revised from the code
// delivered by Geoffrey T. Dairiki for XTide 1.  The commentary has
// been modified to try to keep consistent with the code maintenance,
// but inconsistenties probably remain.

/*************************************************************************
 *
 * Geoffrey T. Dairiki Fri Jul 19 15:44:21 PDT 1996
 *
 ************************************************************************/

/*
 * We will need a function for tidal height as a function of time
 * which is continuous (and has continuous first and second derivatives)
 * for all times.
 *
 * Since the epochs and multipliers for the tidal constituents change
 * with the year, tideDerivative(Interval) has small discontinuities
 * at new year's.  These discontinuities really fry the fast
 * root-finders.
 *
 * We will eliminate the new-year's discontinuities by smoothly
 * interpolating (or "blending") between the tides calculated with one
 * year's coefficients and the tides calculated with the next year's
 * coefficients.
 *
 * i.e. for times near a new year's, we will "blend" a tide as follows:
 *
 * tide(t) = tide(year-1, t)
 *                  + w((t - t0) / Tblend) * (tide(year,t) - tide(year-1,t))
 *
 * Here:  t0 is the time of the nearest new-year.
 *        tide(year-1, t) is the tide calculated using the coefficients
 *           for the year just preceding t0.
 *        tide(year, t) is the tide calculated using the coefficients
 *           for the year which starts at t0.
 *        Tblend is the "blending" time scale.  This is set by
 *           the macro TIDE_BLEND_TIME, currently one hour.
 *        w(x) is the "blending function", whice varies smoothly
 *           from 0, for x < -1 to 1 for x > 1.
 *
 * Derivatives of the blended tide can be evaluated in terms of derivatives
 * of w(x), tide(year-1, t), and tide(year, t).  The blended tide is
 * guaranteed to have as many continuous derivatives as w(x).  */


/* tideDerivative (Interval sinceEpoch, unsigned deriv)
 *
 * Calculate (deriv)th time derivative of the normalized tide for time
 * in s since the beginning (UTC) of currentYear, WITHOUT changing
 * years or blending.
 *
 * Note:  This function does not check for changes in year.  This is
 * important to our algorithm, since for times near new year's, we
 * interpolate between the tides calculated using one year's
 * coefficients and the next year's coefficients.
 */

// DWF 2007-02-15

// XTide spends more time in this method than anywhere else.

// In XTide 2.8.3 and previous, the high-level data types (Speed,
// Amplitude, Interval, etc.) were used to shuttle data around, but at
// the last minute everything reverted to C arrays of doubles just to
// make this loop run faster.  The Great Cleanup of 2006 got rid of
// that hypocrisy.  Most use cases showed no noticeable impact, but
// those that involved generating a really long series of predictions
// (e.g., for stats mode or calendar mode) initially showed alarming
// slowdowns of 300% and worse.  Conversion of select methods and
// functions to inlines, plus the avoidance of one type conversion
// that could not be inlined, shaved the performance hit to about 15%,
// which is close enough to argue that the benefits of high-level data
// types exceed the costs.

const PredictionValue ConstituentSet::tideDerivative (Interval sinceEpoch,
						      unsigned deriv) {
  PredictionValue dt_tide;
  Angle tempd (Units::radians, M_PI / 2.0 * deriv);
  for (unsigned a=0; a<length; ++a) {
    PredictionValue term (amplitudes[a] *
      cos(tempd + _constituents[a].speed * sinceEpoch + phases[a]));
    for (int b = deriv; b > 0; --b)
      term *= _constituents[a].speed.radiansPerSecond();
    dt_tide += term;
  }
  return dt_tide;
}


/* blendWeight (double x, unsigned deriv)
 *
 * Returns the value (deriv)th derivative of the "blending function" w(x):
 *
 *   w(x) =  0,     for x <= -1
 *
 *   w(x) =  1/2 + (15/16) x - (5/8) x^3 + (3/16) x^5,
 *                  for  -1 < x < 1
 *
 *   w(x) =  1,     for x >= 1
 *
 * This function has the following desirable properties:
 *
 *    w(x) is exactly either 0 or 1 for |x| > 1
 *
 *    w(x), as well as its first two derivatives are continuous for all x.
 */

static const double blendWeight (double x, unsigned deriv) {
  double x2 = x * x;

  if (x2 >= 1.0)
      return deriv == 0 && x > 0.0 ? 1.0 : 0.0;

  switch (deriv) {
  case 0:
      return ((3.0 * x2 -10.0) * x2 + 15.0) * x / 16.0 + 0.5;
  case 1:
      return ((x2 - 2.0) * x2 + 1.0) * (15.0/16.0);
  case 2:
      return (x2 - 1.0) * x * (15.0/4.0);
  }
  assert (false);
  return 0.0;
}


/*
 * This function does the actual "blending" of the tide and its
 * derivatives.
 */

const PredictionValue ConstituentSet::blendTide (Timestamp predictTime,
						 unsigned deriv,
						 Year firstYear,
						 double blend) {
  PredictionValue fl[maxDeriv + 1];
  PredictionValue fr[maxDeriv + 1];
  PredictionValue *fp = fl;
  double        w[maxDeriv + 1];
  unsigned      n;

  assert (deriv <= maxDeriv);

  /*
   * If we are already set up for one of the two years
   * of interest, compute that year's tide values first.
   */
  if (currentYear == firstYear + 1)
    fp = fr;
  else if (currentYear != firstYear)
    changeYear (firstYear);

  Interval sinceEpoch (predictTime - epoch);
  for (n = 0; n <= deriv; ++n)
    fp[n] = tideDerivative (sinceEpoch, n);

  /*
   * Compute tide values for the other year of interest,
   *  and the needed values of w(x) and its derivatives.
   */
  if (fp == fl) {
    changeYear (firstYear + 1);
    fp = fr;
  } else {
    changeYear (firstYear);
    fp = fl;
  }
  sinceEpoch = predictTime - epoch;
  for (n = 0; n <= deriv; ++n) {
    fp[n] = tideDerivative (sinceEpoch, n);
    w[n] = blendWeight (blend, n);
  }

  /*
   * Do the blending.
   */
  double fact = 1.0;
  PredictionValue f (fl[deriv]);
  for (n = 0; n <= deriv; ++n) {
    f += fact * w[n] * (fr[deriv-n] - fl[deriv-n]);
    fact *= (double)(deriv - n)/(n+1) * (1.0/tideBlendInterval.s());
  }
  return f;
}


// Calculate (deriv)th time derivative of the normalized tide (for
// time in s).  The result does not have the datum added in and will
// not be converted from KnotsSquared.

const PredictionValue ConstituentSet::tideDerivative (Timestamp predictTime,
						      unsigned deriv) {

  // For starters, get us in the right year.
  Year year (predictTime.year());
  if (year != currentYear)
    changeYear (year);

  Interval sinceEpoch (predictTime - epoch);

  /*
   * If we're close to either the previous or the next
   * new year's we must blend the two years' tides.
   */
  if (sinceEpoch <= tideBlendInterval)
    return prefer (blendTide (predictTime,
                              deriv,
                              currentYear - 1,
			      sinceEpoch / tideBlendInterval),
                   preferredLengthUnits);
  else
    if (!(nextEpoch.isNull())) {
      Interval tillNextEpoch (nextEpoch - predictTime);
      if (tillNextEpoch <= tideBlendInterval)
        return prefer (blendTide (predictTime,
                                  deriv,
                                  currentYear,
				  -(tillNextEpoch / tideBlendInterval)),
                       preferredLengthUnits);
    }

  /*
   * Else, we're far enough from newyear's to ignore the blending.
   */
  return prefer (tideDerivative (sinceEpoch, deriv), preferredLengthUnits);
}


#ifdef blendingTest
void ConstituentSet::tideDerivativeBlendValues (
                                     Timestamp predictTime,
				     unsigned deriv,
				     NullablePredictionValue &firstYear_out,
				     NullablePredictionValue &secondYear_out) {
  firstYear_out.makeNull();
  secondYear_out.makeNull();
  Year year (predictTime.year());
  if (year != currentYear)
    changeYear (year);
  if ((predictTime - epoch) <= tideBlendInterval) {
    secondYear_out = tideDerivative (predictTime - epoch, deriv);
    changeYear (year - 1);
    firstYear_out = tideDerivative (predictTime - epoch, deriv);
  } else if (!(nextEpoch.isNull())) {
    if ((nextEpoch - predictTime) <= tideBlendInterval) {
      firstYear_out = tideDerivative (predictTime - epoch, deriv);
      changeYear (year + 1);
      secondYear_out = tideDerivative (predictTime - epoch, deriv);
    }
  }
  if (!firstYear_out.isNull())
    firstYear_out = prefer (firstYear_out, preferredLengthUnits);
  if (!secondYear_out.isNull())
    secondYear_out = prefer (secondYear_out, preferredLengthUnits);
}
#endif

}
