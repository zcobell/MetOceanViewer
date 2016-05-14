// $Id: SubordinateStation.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  SubordinateStation  Station with offsets.

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
#include "SubordinateStation.hh"

namespace libxtide {


SubordinateStation::SubordinateStation (const Dstr &name_,
                                        const StationRef &stationRef,
                                        const ConstituentSet &constituents,
                                        const Dstr &note_,
                                        CurrentBearing minCurrentBearing_,
					CurrentBearing maxCurrentBearing_,
                                        const MetaFieldVector &metadata,
                                        const HairyOffsets &offsets):
  Station (name_,
           stationRef,
           constituents,
           note_,
           minCurrentBearing_,
           maxCurrentBearing_,
           metadata),
  _offsets(offsets) {

  // Calculate minimumTimeOffset and maximumTimeOffset.
  minimumTimeOffset = maximumTimeOffset = _offsets.maxTimeAdd();
  if (_offsets.minTimeAdd() < minimumTimeOffset)
    minimumTimeOffset = _offsets.minTimeAdd();
  if (_offsets.minTimeAdd() > maximumTimeOffset)
    maximumTimeOffset = _offsets.minTimeAdd();
  if (isCurrent) {
    if (!_offsets.floodBegins().isNull()) {
      if (_offsets.floodBegins() < minimumTimeOffset)
	minimumTimeOffset = _offsets.floodBegins();
      if (_offsets.floodBegins() > maximumTimeOffset)
	maximumTimeOffset = _offsets.floodBegins();
    }
    if (!_offsets.ebbBegins().isNull()) {
      if (_offsets.ebbBegins() < minimumTimeOffset)
	minimumTimeOffset = _offsets.ebbBegins();
      if (_offsets.ebbBegins() > maximumTimeOffset)
	maximumTimeOffset = _offsets.ebbBegins();
    }
  }

  assert (isCurrent || (_offsets.floodBegins().isNull() &&
	                _offsets.ebbBegins().isNull()));
}


Station * const SubordinateStation::clone() const {
  return new SubordinateStation (*this);
}


const PredictionValue SubordinateStation::minLevelHeuristic() const {
  PredictionValue pv (Station::minLevelHeuristic());
  pv *= _offsets.minLevelMultiply();
  pv.convertAndAdd (_offsets.minLevelAdd());
  return pv;
}


const PredictionValue SubordinateStation::maxLevelHeuristic() const {
  PredictionValue pv (Station::maxLevelHeuristic());
  pv *= _offsets.maxLevelMultiply();
  pv.convertAndAdd (_offsets.maxLevelAdd());
  return pv;
}


const bool SubordinateStation::isSubordinateStation() {
  return true;
}


const bool SubordinateStation::haveFloodBegins() {
  return !_offsets.floodBegins().isNull();
}


const bool SubordinateStation::haveEbbBegins() {
  return !_offsets.ebbBegins().isNull();
}


const PredictionValue SubordinateStation::predictTideLevel (
						       Timestamp predictTime) {

  // If units changed, trigger a refresh.
  if (cacheUnits != predictUnits()) {
    subleftt.makeNull();
    cacheUnits = predictUnits();
  }

  // If we are outside the cached bracket, trigger a refresh.
  if (!subleftt.isNull())
    if (predictTime < subleftt || predictTime >= subrightt)
      subleftt.makeNull();

  // Refresh cached bracket?
  if (subleftt.isNull()) {

    // Since we have no way of knowing whether predictTime will follow
    // a contiguous range or jump around, there's no point preserving
    // the organizer beyond a single bracket.  This puts some noise in
    // the results since everything will come back slightly different
    // even when predictTime just walks out of the bracket.
    TideEventsOrganizer organizer;

    // Initialize organizer with a starting range.  (Yes, this is
    // necessary.)  Unlikely that a real station could go 48 hours
    // with no tide events, but detect and correct if it does.
    Interval delta;
    for (delta = Global::day; organizer.empty(); delta *= 2U)
      predictTideEvents (predictTime - delta,
                         predictTime + delta,
                         organizer,
                         knownTideEvents);

    while (subleftt.isNull()) {

      // If there are multiple events with the same eventTime, it
      // doesn't matter which one we pick, because they will all be
      // different next time anyway.

      // upper_bound: first element whose key is greater than predictTime.
      TideEventsIterator right = organizer.upper_bound (predictTime);
      delta = Global::day;
      while (right == organizer.end()) {
	// Need more future
	extendRange (organizer, forward, delta, knownTideEvents);
        delta *= 2U;
	right = organizer.upper_bound (predictTime);
      }

      // lower_bound: first element whose key is not less than predictTime.
      TideEventsIterator left = organizer.lower_bound (predictTime);
      // If upper bound existed, this must also exist.
      assert (left != organizer.end());
      // But what we usually want is the previous one.
      if (left->second.eventTime > predictTime) {
        bool recycle = false;
        delta = Global::day;
	while (left == organizer.begin()) {
	  // Need more past
	  extendRange (organizer, backward, delta, knownTideEvents);
          delta *= 2U;
          left = organizer.lower_bound (predictTime);
          recycle = true;
	}

        // If the bracket was skewed way off, it's possible that
        // extending the range backward could change the result for
        // right.
        if (recycle)
          continue;

	--left;
      }

      // Populate the cached bracket.
      subleftt = left->second.eventTime;
      subleftp = left->second.eventLevel;
      subrightt = right->second.eventTime;
      subrightp = right->second.eventLevel;
      uncleftt = left->second.uncorrectedEventTime;
      uncleftp = left->second.uncorrectedEventLevel;
      uncrightt = right->second.uncorrectedEventTime;
      uncrightp = right->second.uncorrectedEventLevel;
    }
  }
  assert (subleftt <= predictTime && predictTime < subrightt);

  // All manner of pathologies are possible.  We might have skipped over some
  // conflicting events with the same eventTime.  uncleftt might be later
  // than uncrightt.  The left and right events might even be the same type.
  // But suppressing garbage output is not straightforward.  For example, for
  // each bracket where uncleftt is later than uncrightt, there is a
  // neighboring bracket that includes an untracked event.  The untracked
  // event is usually close to the boundary, so the middle and other end of
  // the bracket are still approximately correct.  Throw it all out?  It's
  // not obvious what it should be replaced by.  Interpolate--from what to
  // what?

  // The only case that needs fixing to prevent program failure is the one
  // where we divide by zero.  If the uncorrected prediction values were the
  // same, make a straight line between the corrected values, whatever they
  // are.  (Probably they are also the same, but you never know.)
  if (uncrightp == uncleftp)
    return subleftp + (subrightp - subleftp) *
      ((predictTime-subleftt) / (subrightt-subleftt));

  // Otherwise, map the time in and map the pv out.
  return subleftp + (subrightp - subleftp) * (
    (Station::predictTideLevel (uncleftt + (uncrightt - uncleftt) *
				((predictTime-subleftt)/(subrightt-subleftt)))
     -uncleftp) / (uncrightp-uncleftp));
}


// All the nullification in this method serves to guarantee that we
// don't ever use garbage values in predictTideLevel.  Try to use a
// null uncorrectedEventLevel for anything and foom, assertion
// failure.  In addition, the Calendar constructor requires that
// uncorrectedEventTime be null if it is not applicable.

void SubordinateStation::finishTideEvent (TideEvent &te) {
  te.isCurrent = isCurrent;
  if (te.isSunMoonEvent()) {
    te.eventLevel.makeNull();
    te.uncorrectedEventTime.makeNull();
    te.uncorrectedEventLevel.makeNull();
  } else {
    switch (te.eventType) {
    case TideEvent::rawreading:
      te.eventLevel = predictTideLevel (te.eventTime);
      te.uncorrectedEventTime.makeNull();
      te.uncorrectedEventLevel.makeNull();
      break;
    case TideEvent::max:
      te.uncorrectedEventTime = te.eventTime;
      te.uncorrectedEventLevel = te.eventLevel
			       = Station::predictTideLevel (te.eventTime);
      if (te.isMinCurrentEvent()) {
	// Handling of min currents is questionable; see
	// http://www.flaterco.com/xtide/mincurrents.html
	if (_offsets.ebbBegins().isNull())
	  te.eventTime += _offsets.minTimeAdd();
	else
	  te.eventTime += _offsets.ebbBegins();
	te.eventLevel *= _offsets.minLevelMultiply();
	te.eventLevel.convertAndAdd (_offsets.minLevelAdd());
      } else {
	te.eventTime += _offsets.maxTimeAdd();
	te.eventLevel *= _offsets.maxLevelMultiply();
	te.eventLevel.convertAndAdd (_offsets.maxLevelAdd());
      }
      break;
    case TideEvent::min:
      te.uncorrectedEventTime = te.eventTime;
      te.uncorrectedEventLevel = te.eventLevel
			       = Station::predictTideLevel (te.eventTime);
      if (te.isMinCurrentEvent()) {
	// Handling of min currents is questionable; see
	// http://www.flaterco.com/xtide/mincurrents.html
	if (_offsets.floodBegins().isNull())
	  te.eventTime += _offsets.maxTimeAdd();
	else
	  te.eventTime += _offsets.floodBegins();
	te.eventLevel *= _offsets.maxLevelMultiply();
	te.eventLevel.convertAndAdd (_offsets.maxLevelAdd());
      } else {
	te.eventTime += _offsets.minTimeAdd();
	te.eventLevel *= _offsets.minLevelMultiply();
	te.eventLevel.convertAndAdd (_offsets.minLevelAdd());
      }
      break;
    case TideEvent::slackrise:
      if (haveFloodBegins()) {
        te.uncorrectedEventTime = te.eventTime;
        te.uncorrectedEventLevel = te.eventLevel
			         = Station::predictTideLevel (te.eventTime);
	te.eventTime += _offsets.floodBegins();
      } else {
        te.uncorrectedEventTime.makeNull();
        te.uncorrectedEventLevel.makeNull();
	// eventTime was fixed in
	// findInterpolatedSubstationMarkCrossing (so hopefully this
	// will be zero)
	te.eventLevel = predictTideLevel (te.eventTime);
      }
      break;
    case TideEvent::slackfall:
      if (haveEbbBegins()) {
        te.uncorrectedEventTime = te.eventTime;
        te.uncorrectedEventLevel = te.eventLevel
			         = Station::predictTideLevel (te.eventTime);
	te.eventTime += _offsets.ebbBegins();
      } else {
        te.uncorrectedEventTime.makeNull();
        te.uncorrectedEventLevel.makeNull();
	// eventTime was fixed in
	// findInterpolatedSubstationMarkCrossing (so hopefully this
	// will be zero)
	te.eventLevel = predictTideLevel (te.eventTime);
      }
      break;
    case TideEvent::markrise:
    case TideEvent::markfall:
      te.uncorrectedEventTime.makeNull();
      te.uncorrectedEventLevel.makeNull();
      // eventTime was fixed in
      // Station::findInterpolatedSubstationMarkCrossing
      te.eventLevel = predictTideLevel (te.eventTime);
      break;
    default:
      assert (false);
    }
  }
}


const Timestamp SubordinateStation::findInterpolatedSubstationMarkCrossing (
                                        const TideEvent &tideEvent1,
                                        const TideEvent &tideEvent2,
                                        PredictionValue marklev,
                                        bool &isRising_out) {
  Timestamp eventTime;

  // Toss any brackets in which reverse interpolation will blow up.
  if (tideEvent2.eventLevel != tideEvent1.eventLevel) {

    // This time we map the pv in and map the time out.
    eventTime = findSimpleMarkCrossing (tideEvent1.uncorrectedEventTime,
                                        tideEvent2.uncorrectedEventTime,
      tideEvent1.uncorrectedEventLevel +
        (tideEvent2.uncorrectedEventLevel - tideEvent1.uncorrectedEventLevel) *
        ((marklev - tideEvent1.eventLevel) /
         (tideEvent2.eventLevel - tideEvent1.eventLevel)),
                                        isRising_out);

    if (!(eventTime.isNull())) {
      eventTime = tideEvent1.eventTime +
        (tideEvent2.eventTime - tideEvent1.eventTime) *
	((eventTime - tideEvent1.uncorrectedEventTime) /
         (tideEvent2.uncorrectedEventTime - tideEvent1.uncorrectedEventTime));
    }
  }

  return eventTime;
}


void SubordinateStation::addInterpolatedSubstationMarkCrossingEvents (
                                           Timestamp startTime,
                                           Timestamp endTime,
                                           TideEventsOrganizer &organizer) {
  bool isRising;
  TideEvent new_te;

  // Problem #1.  Need to extract the set of relevant events.

  // (Even if the order of invocations in predictTideEvents were
  // changed to add the sun and moon events afterward, those events as
  // well as future and past ranges could be hanging around from
  // previous calls.)

  TideEventsOrganizer relevantEvents;
  TideEventsIterator it = organizer.lower_bound(startTime);
  TideEventsIterator stop = organizer.lower_bound(endTime);
  while (it != stop) {
    TideEvent &te = it->second;
    switch (te.eventType) {
    case TideEvent::max:
    case TideEvent::min:
      relevantEvents.add (te);
      break;
    case TideEvent::slackrise:
      if (haveFloodBegins())
	relevantEvents.add (te);
      break;
    case TideEvent::slackfall:
      if (haveEbbBegins())
	relevantEvents.add (te);
      break;
    default:
      ;
    }
    ++it;
  }

  // Problem #2.  Need to initialize it if empty.
  Interval delta;
  for (delta = Global::day; relevantEvents.empty(); delta *= 2U)
    predictTideEvents (startTime - delta,
                       endTime + delta,
                       relevantEvents,
                       knownTideEvents);

  // Problem #3.  We need to extend the range to be sure of getting
  // the first and last mark crossing and slack.
  for (delta = Global::day;
       relevantEvents.begin()->second.eventTime >= startTime;
       delta *= 2U)
    extendRange (relevantEvents, backward, delta, knownTideEvents);
  for (delta = Global::day;
       relevantEvents.rbegin()->second.eventTime < endTime;
       delta *= 2U)
    extendRange (relevantEvents, forward, delta, knownTideEvents);

  // OK great.
  it = relevantEvents.begin();
  TideEvent left_te = it->second;
  while (++it != relevantEvents.end()) {
    TideEvent right_te = it->second;

    assert (!left_te.uncorrectedEventTime.isNull());
    assert (!right_te.uncorrectedEventTime.isNull());
    assert (!left_te.uncorrectedEventLevel.isNull());
    assert (!right_te.uncorrectedEventLevel.isNull());

    // We have a bracket as used in
    // SubordinateStation::predictTideLevel.  However, it isn't
    // necessarily a nice bracket for findMarkCrossing_Dairiki.
    // findMarkCrossing_Dairiki should return null when there's no
    // good answer.

    // Check for slacks, if applicable.
    if (isCurrent && (
      (left_te.eventType == TideEvent::max && !haveEbbBegins()) ||
      (left_te.eventType == TideEvent::min && !haveFloodBegins()))) {
      new_te.eventTime = findInterpolatedSubstationMarkCrossing (left_te,
                                                                 right_te,
                                          PredictionValue(predictUnits(), 0.0),
                                                                 isRising);
      if (!(new_te.eventTime.isNull())) {
	new_te.eventType = (isRising ? TideEvent::slackrise
				     : TideEvent::slackfall);
	finishTideEvent (new_te);
	if (new_te.eventTime >= startTime && new_te.eventTime < endTime)
	  organizer.add (new_te);
      }
    }

    // Check for mark, if applicable.
    if (!markLevel.isNull()) {
      new_te.eventTime = findInterpolatedSubstationMarkCrossing (left_te,
								 right_te,
								 markLevel,
								 isRising);
      if (!(new_te.eventTime.isNull())) {
	new_te.eventType = (isRising ? TideEvent::markrise
				     : TideEvent::markfall);
	finishTideEvent (new_te);
	if (new_te.eventTime >= startTime && new_te.eventTime < endTime)
	  organizer.add (new_te);
      }
    }

    left_te = right_te;
  }
}


void SubordinateStation::predictTideEvents (Timestamp startTime,
                                            Timestamp endTime,
                                            TideEventsOrganizer &organizer,
                                            TideEventsFilter filter) {

  Station::predictTideEvents (startTime, endTime, organizer, filter);

  if (filter == noFilter &&
      (!markLevel.isNull() || !haveFloodBegins() || !haveEbbBegins()))
    addInterpolatedSubstationMarkCrossingEvents (startTime,
                                                 endTime,
                                                 organizer);
}


// There's a lot of symmetrical and semi-duplicate code going on here
// and in Station::addSimpleTideEvents, but I don't know that it can
// be improved much by adding more methods.  You end up passing so
// many values that you may as well have done it in-place.  I'm going
// to call it done.

}
