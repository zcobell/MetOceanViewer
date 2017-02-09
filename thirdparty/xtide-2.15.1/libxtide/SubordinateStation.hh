// $Id: SubordinateStation.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {

class SubordinateStation: public Station {
public:

  // See HarmonicsFile::getStation.
  SubordinateStation (const Dstr &name_,
                      const StationRef &stationRef,
                      const ConstituentSet &constituents,
                      const Dstr &note_,
                      CurrentBearing minCurrentBearing_,
		      CurrentBearing maxCurrentBearing_,
                      const MetaFieldVector &metadata,
                      const HairyOffsets &offsets);

  // All these public methods are replacing virtual methods of
  // Station.  See Station.hh for descriptions.

  Station * const clone() const;

  const PredictionValue minLevelHeuristic() const;
  const PredictionValue maxLevelHeuristic() const;

  const PredictionValue predictTideLevel (Timestamp predictTime);

  void predictTideEvents (Timestamp startTime,
                          Timestamp endTime,
                          TideEventsOrganizer &organizer,
                          TideEventsFilter filter = noFilter);

protected:

  const HairyOffsets _offsets;

  // predictTideLevel cached bracket.  The code has gotten wordy
  // enough already without renaming these all to
  // uncorrectedLeftEventTime, etc.
  Timestamp       uncleftt, uncrightt, subleftt, subrightt;
  PredictionValue uncleftp, uncrightp, subleftp, subrightp;
  Units::PredictionUnits cacheUnits;

  const bool isSubordinateStation();
  const bool haveFloodBegins();
  const bool haveEbbBegins();

  // Wrapper for findSimpleMarkCrossing that does necessary
  // compensations for substation interpolation.  Like
  // findSimpleMarkCrossing and findMarkCrossing_Dairiki, this method
  // is insensitive to the relative ordering of tideEvent1 and
  // tideEvent2.
  const Timestamp findInterpolatedSubstationMarkCrossing (
			const TideEvent &tideEvent1,
			const TideEvent &tideEvent2,
			PredictionValue marklev,
			bool &isRising_out);

  // Submethod of predictTideEvents.
  void addInterpolatedSubstationMarkCrossingEvents (
                        Timestamp startTime,
                        Timestamp endTime,
                        TideEventsOrganizer &organizer);

  // Given eventTime and eventType, fill in other fields and possibly
  // apply corrections.
  void finishTideEvent (TideEvent &te);
};

}
