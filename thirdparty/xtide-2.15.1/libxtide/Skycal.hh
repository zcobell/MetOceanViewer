// $Id: Skycal.hh 5748 2014-10-11 19:38:53Z flaterco $

// Skycal.hh -- Functions for sun and moon events.
// Please see Skycal.cc for verbose commentary.

// Prediction of moon phases, sun and moon rises and sets has nothing
// to do with tide prediction.  There is no overlap between this code
// and the tide prediction code.

namespace libxtide {

namespace Skycal {

  // eventTime and eventType are set to the next moon phase event
  // following time t.  Nothing else in tideEvent_out is changed.
  void findNextMoonPhase (Timestamp t, TideEvent &tideEvent_out);

  // eventTime and eventType are set to the next (sun/moon) rise or
  // set event following time t.  Nothing else in tideEvent_out is
  // changed.
  enum RiseSetType {solar, lunar};
  void findNextRiseOrSet (Timestamp t,
                          const Coordinates &c,
                          RiseSetType riseSetType,
                          TideEvent &tideEvent_out);

  // Returns true if sun is up at time t.
  const bool sunIsUp (Timestamp t, const Coordinates &c);

#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
  // 2014-06-22:  Fast path to find bracketing new moon events.
  // prev_out <= t < next_out.
  void findNewMoons (Timestamp t, Timestamp &prev_out, Timestamp &next_out);
#endif

}

}
