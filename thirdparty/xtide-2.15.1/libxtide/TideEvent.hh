// $Id: TideEvent.hh 5748 2014-10-11 19:38:53Z flaterco $

// TideEvent  Generic representation for tide events.

/*
    Copyright (C) 2004  David Flater.

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

class TideEvent {
public:

  // CamelCasing waived here for consistency with sunrise, sunset.
  enum EventType {max, min, slackrise, slackfall, markrise, markfall,
    sunrise, sunset, moonrise, moonset, newmoon, firstquarter, fullmoon,
    lastquarter, rawreading};

  Timestamp               eventTime;
  EventType               eventType;
  NullablePredictionValue eventLevel;
  bool                    isCurrent;

  // For sub stations with residual offsets, these record the time and
  // level of the corresponding event before corrections are applied.
  // This is not necessarily the same as the reference station:  the
  // harmonic constants may still have been adjusted.  When not
  // applicable, these variables remain null.
  Timestamp               uncorrectedEventTime;
  NullablePredictionValue uncorrectedEventLevel;

  // Generate one line of text output, applying global formatting
  // rules and so on.
  // Legal forms are c (CSV), t (text) or i (iCalendar).
  // Legal modes are p (plain), r (raw), or m (medium rare).
  // text_out is not newline terminated.
  //
  // print needs timezone and sometimes name and coordinates from station.
  void print (Dstr &text_out,
              Mode::Mode mode,
              Format::Format form,
              const Station &station) const;

  constString longDescription () const;
  constString shortDescription () const;
  const bool isSunMoonEvent () const;
  const bool isMaxMinEvent () const;

  // This returns true if the description of the event would be Min Flood or
  // Min Ebb.
  const bool isMinCurrentEvent () const;
};

}
