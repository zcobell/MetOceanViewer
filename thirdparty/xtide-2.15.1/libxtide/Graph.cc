// $Id: Graph.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  Graph  Abstract superclass for all graphs.

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
#include "Graph.hh"
#include "Skycal.hh"

namespace libxtide {


// Margin left at top and bottom of tide graphs when scaling tides;
// how much "water" at lowest tide; how much "sky" at highest tide.
// This is a scaling factor for the graph height.
// Adjusted 2009-09-03:  Previously, the value of 0.0673 was eating
// some of the slack produced by an exceedingly conservative estimate
// of tidal range.  With the improved heuristic for estimating tidal
// range, that slack is gone.
const double Graph::vertGraphMargin (0.1);


Graph::Graph (unsigned xSize, unsigned ySize, GraphStyle style):
  _style(style),
  _xSize(xSize),
  _ySize(ySize) {}


Graph::~Graph() {}


static void findNextSunEvent (TideEventsIterator &it,
                              TideEventsOrganizer &organizer,
                              Timestamp now,
                              Timestamp endTime,
                              Timestamp &nextSunEventTime_out) {
  while (it != organizer.end()) {
    TideEvent &te = it->second;
    if (te.eventTime > now &&
         (te.eventType == TideEvent::sunrise ||
          te.eventType == TideEvent::sunset)) {
      nextSunEventTime_out = te.eventTime;
      return;
    }
    ++it;
  }
  nextSunEventTime_out = endTime + Global::day;
}


const double Graph::linterp (double lo, double hi, double opacity) const {
  return lo + opacity * (hi - lo);
}


const unsigned char Graph::linterp (unsigned char lo,
				    unsigned char hi,
				    double opacity) const {
  return (unsigned char) linterp ((double)lo, (double)hi, opacity);
}


const unsigned short Graph::linterp (unsigned short lo,
				     unsigned short hi,
				     double opacity) const {
  return (unsigned short) linterp ((double)lo, (double)hi, opacity);
}


// Translate a tide depth to a y-coordinate.
#define xlate(y) linterp (ymax, \
                          ymin, \
                          (((y) - valmin) / (valmax - valmin)))


void Graph::checkDepth (double ymax,
                        double ymin,
                        double valmax,
                        double valmin,
			unsigned lineStep,
                        int &minDepth_out,
                        int &maxDepth_out) const {
  minDepth_out=INT_MAX;
  maxDepth_out=INT_MIN;
  int depth;
  const double valmax10(valmax*10), valmin10(valmin*10);
  double ytide;
  for (depth = 0; depth <= valmax10; depth += lineStep) {
    ytide = xlate(0.1*depth);
    // Leave room for 3 lines of text at top, 3 lines of text plus
    // tick marks at bottom.
    if (ytide - fontHeight()/2 - depthLineVerticalMargin() <=
                                      fontHeight() * 3)
      break;
    if (ytide + fontHeight()/2 + depthLineVerticalMargin() >=
	_ySize - fontHeight() * 3 - hourTickLen() - hourTickVerticalMargin())
      continue;
    maxDepth_out = depth;
    if (depth < minDepth_out) // In case one loop is never executed.
      minDepth_out = depth;
  }
  for (depth = -lineStep; depth >= valmin10; depth -= lineStep) {
    ytide = xlate(0.1*depth);
    // Leave room for 3 lines of text at top, 3 lines of text plus
    // tick marks at bottom.
    if (ytide - fontHeight()/2 - depthLineVerticalMargin() <=
                                      fontHeight() * 3)
      continue;
    if (ytide + fontHeight()/2 + depthLineVerticalMargin() >=
	_ySize - fontHeight() * 3 - hourTickLen() - hourTickVerticalMargin())
      break;
    minDepth_out = depth;
    if (depth > maxDepth_out) // In case one loop is never executed.
      maxDepth_out = depth;
  }
}


void Graph::drawDepth (double ymax,
                       double ymin,
                       double valmax,
                       double valmin,
                       unsigned lineStep,
                       unsigned labelWidth,
		       int minDepth,
		       int maxDepth) {
  for (int depth=minDepth; depth<=maxDepth; depth+=lineStep)
    drawHorizontalLinePxSy (labelWidth,
			    _xSize-1,
			    xlate(0.1*depth),
			    Colors::foreground);
}


void Graph::clearGraph (Timestamp startTime,
                        Timestamp endTime,
                        Interval increment,
                        Station *station,
                        TideEventsOrganizer &organizer) {
  assert (station);

  // Shortcut if event mask is set to suppress sunrises or sunsets.
  if (Global::settings["em"].s.contains("s")) {
    drawBoxS (0, _xSize, 0, _ySize, Colors::daytime);
    return;
  }

  // Clear the graph by laying down a background of days and nights.
  bool sunIsUp = true;
  if (!(station->coordinates.isNull()))
    sunIsUp = Skycal::sunIsUp (startTime, station->coordinates);

  Timestamp loopTime (startTime);
  Timestamp nextSunEventTime;
  TideEventsIterator it (organizer.begin());
  while (loopTime < endTime) {
    findNextSunEvent (it, organizer, loopTime, endTime, nextSunEventTime);
    assert (loopTime < nextSunEventTime);
    if (it != organizer.end()) {
      switch (it->second.eventType) {
      case TideEvent::sunrise:
	sunIsUp = false;
        break;
      case TideEvent::sunset:
	sunIsUp = true;
	break;
      default:
	assert (false);
      }
    } else
      sunIsUp = !sunIsUp;

    Colors::Colorchoice c = (sunIsUp ? Colors::daytime : Colors::nighttime);
    double x1 = (loopTime-startTime)/increment;
    double x2 = std::min ((double)_xSize, (nextSunEventTime-startTime)/increment);
    drawBoxS (x1, x2, 0, _ySize, c);
    loopTime = nextSunEventTime;
  }
}


static void makeDepthLabel (int depth,
			    unsigned lineStep,
			    const Dstr &unitsDesc,
			    Dstr &text_out) {
  text_out = "";
  if (depth < 0) {
    text_out += '-';
    depth = -depth;
  }
  text_out += depth / 10;
  if (lineStep < 10) {
    text_out += '.';
    text_out += depth % 10;
  }
  if (Global::settings["ou"].c != 'y') {
    text_out += ' ';
    text_out += unitsDesc;
  }
}


void Graph::figureLabels (double ymax,
			  double ymin,
			  double valmax,
			  double valmin,
                          const Dstr &unitsDesc,
			  unsigned &lineStep_out,
			  unsigned &labelWidth_out,
			  unsigned &labelRight_out,
			  int &minDepth_out,
			  int &maxDepth_out) const {
  assert (valmin < valmax);
  const double yzulu (xlate(0));

  // Tortured logic to figure the increment for depth lines.  We want
  // nice increments like 2, 5, 10, not random numbers.
  if (Global::settings["gt"].c == 'y')
    lineStep_out = 1;
  else
    lineStep_out = 10;
  {
    unsigned prevStep (lineStep_out), prevMult (10);
    while (yzulu - xlate(0.1*lineStep_out) <
	   oughtHeight() + oughtVerticalMargin()) {
      switch (prevMult) {
      case 10:
	prevMult = 2;
	lineStep_out = prevStep * prevMult;
	break;
      case 2:
	prevMult = 5;
	lineStep_out = prevStep * prevMult;
	break;
      case 5:
	prevMult = 10;
	prevStep = lineStep_out = prevStep * prevMult;
	break;
      default:
	assert (false);
      }
    }
  }

  // More figuring.
  checkDepth (ymax, ymin, valmax, valmin, lineStep_out, minDepth_out,
	      maxDepth_out);
  labelWidth_out = labelRight_out = 0;
  if (minDepth_out <= maxDepth_out) {
    {
      Dstr minLabel;
      makeDepthLabel (minDepth_out, lineStep_out, unitsDesc, minLabel);
      labelWidth_out = stringWidth(minLabel);
    }{
      Dstr maxLabel;
      makeDepthLabel (maxDepth_out, lineStep_out, unitsDesc, maxLabel);
      unsigned maxLabelWidth (stringWidth(maxLabel));
      if (maxLabelWidth > labelWidth_out)
	labelWidth_out = maxLabelWidth;
    }
    labelRight_out = labelWidth_out + depthLabelLeftMargin();
    labelWidth_out = labelRight_out + depthLabelRightMargin();
  }
  // Otherwise, there are no depth lines, but labelWidth (0) will be
  // used for extra lines and the now (+) mark.
}


// Blurbs push and shove their neighbors until everyone has room.

static const bool iterateEventBlurbCollisions (
				       SafeVector<Graph::EventBlurb> &blurbs) {
  bool collision (false);
  for (unsigned long i=1; i<blurbs.size(); ++i) {
    Graph::EventBlurb &prev (blurbs[i-1]);
    Graph::EventBlurb &cur (blurbs[i]);
    if (prev.x > cur.x) // Try to keep them in order at least.
      std::swap (prev.x, cur.x);
    int collisionWidth ((prev.x + prev.deltaRight) -
			(cur.x + cur.deltaLeft) + 1);
    if (collisionWidth > 0) {
      collision = true;
      int leftAdjust (-collisionWidth / 2);
      int rightAdjust (collisionWidth + leftAdjust);
      prev.x += leftAdjust;
      cur.x  += rightAdjust;
    }
  }
  return collision;
}


// With no iteration limit, this would converge eventually.  There is
// a first event and a last event, and those boundary events would
// eventually be pushed far enough out into the void to make room for
// everyone.  However, the distance that events could migrate from
// their original locations would be unbounded.  The optimal iteration
// limit to maintain some semblance of order is anybody's guess.  Even
// when it fails, the end result is better than what we started with.

static void fixEventBlurbCollisions (SafeVector<Graph::EventBlurb> &blurbs) {
  for (unsigned i=0; i<20 && iterateEventBlurbCollisions(blurbs); ++i);
}


void Graph::drawBlurbs (int topLine, SafeVector<EventBlurb> &blurbs) {
  fixEventBlurbCollisions (blurbs);
  for (SafeVector<EventBlurb>::iterator blurbit = blurbs.begin();
       blurbit != blurbs.end();
       ++blurbit)
    labelEvent (topLine, *blurbit);
}


void Graph::drawTides (Station *station,
                       Timestamp nominalStartTime,
                       Angle *angle) {
  assert (station);
  assert (station->aspect > 0.0);

  // Figure constants.
  const double ymin (vertGraphMargin * (double)_ySize);
  const double ymax ((double)_ySize - ymin);
  const double valmin (station->minLevelHeuristic().val());
  const double valmax (station->maxLevelHeuristic().val());
  assert (valmin < valmax);

  unsigned lineStep, labelWidth, labelRight;
  int minDepth, maxDepth;
  const Dstr unitsDesc (Units::shortName (station->predictUnits()));
  figureLabels (ymax, ymin, valmax, valmin, unitsDesc, lineStep, labelWidth,
		labelRight, minDepth, maxDepth);

  Interval increment (std::max ((interval_rep_t)1,
           Global::intervalround (Global::aspectMagicNumber /
                                  (double)_ySize /
  			          (aspectFudgeFactor() * station->aspect))));
  Timestamp startTime (nominalStartTime -
		       increment * startPosition(labelWidth));
  Timestamp endTime (startTime + increment * _xSize);
  Timestamp currentTime ((time_t)time(NULL));

  // First get a list of the relevant tide events.  Need some extra on
  // either side since text pertaining to events occurring beyond the
  // margins can still be visible.  We also need to make sure
  // *something* shows up so that extendRange can work below.

  TideEventsOrganizer organizer;
  Interval delta;
  for (delta = Global::day; organizer.empty(); delta *= 2U)
    station->predictTideEvents (startTime - delta, endTime + delta, organizer);

  // Need at least one following max and min for clock mode.
  TideEvent nextMax, nextMin;
  if (_style == clock) {
    bool doneMax = false, doneMin = false;
    delta = Global::day;
    while (!(doneMax && doneMin)) {
      TideEventsIterator it = organizer.upper_bound(currentTime);
      while (it != organizer.end() && !(doneMax && doneMin)) {
        TideEvent &te = it->second;
        if (!doneMax && te.eventType == TideEvent::max) {
          doneMax = true;
          nextMax = te;
        } else if (!doneMin && te.eventType == TideEvent::min) {
          doneMin = true;
          nextMin = te;
        }
        ++it;
      }
      if (!(doneMax && doneMin)) {
        station->extendRange (organizer, Station::forward, delta);
        delta *= 2U;
      }
    }

    // Need a max/min pair bracketing current time for tide clock icon
    // angle kludge.
    if (angle) {
      TideEvent nextMaxOrMin;
      if (nextMax.eventTime < nextMin.eventTime)
	nextMaxOrMin = nextMax;
      else
	nextMaxOrMin = nextMin;
      TideEvent previousMaxOrMin;
      {
	bool done = false;
        delta = Global::day;
	while (!done) {
	  TideEventsIterator it = organizer.upper_bound(currentTime);
	  assert (it != organizer.end());
	  while (it != organizer.begin() && !done)
	    if ((--it)->second.isMaxMinEvent()) {
	      done = true;
	      previousMaxOrMin = it->second;
	    }
	  if (!done) {
	    station->extendRange (organizer, Station::backward, delta);
            delta *= 2U;
          }
	}
      }

      // This could blow up on pathological subordinate stations.
      // Better to let it slide.  (The clock will do something weird
      // but won't die.)
      // assert (previousMaxOrMin.eventType != nextMaxOrMin.eventType);

      assert (previousMaxOrMin.eventTime <= currentTime &&
              nextMaxOrMin.eventTime > currentTime);
      assert (previousMaxOrMin.isMaxMinEvent());
      assert (nextMaxOrMin.isMaxMinEvent());

      double temp ((currentTime - previousMaxOrMin.eventTime) /
		   (nextMaxOrMin.eventTime - previousMaxOrMin.eventTime));
      temp *= 180.0;
      if (previousMaxOrMin.eventType == TideEvent::min)
        temp += 180.0;
      (*angle) = Angle (Units::degrees, temp);
    }
  }

  clearGraph (startTime, endTime, increment, station, organizer);

  // Draw depth lines sooner?
  const char tl (Global::settings["tl"].c);
  if (tl == 'n')
    drawDepth (ymax, ymin, valmax, valmin, lineStep, labelWidth, minDepth,
	       maxDepth);

  // Draw the actual tides.
  {
    SafeVector<double> val(_xSize+3), y(_xSize+3);
#ifdef blendingTest
    SafeVector<BlendBlob> blendBlobs(_xSize+3);
#endif
    unsigned x;
    Timestamp loopt;
    for (x=0, loopt=startTime-increment;
	 x < _xSize+3; ++x, loopt += increment) {
      val[x] = station->predictTideLevel(loopt).val();
      y[x]   = xlate(val[x]);
#ifdef blendingTest
      NullablePredictionValue firstYear, secondYear;
      station->tideLevelBlendValues (loopt, firstYear, secondYear);
      assert (firstYear.isNull() == secondYear.isNull());
      blendBlobs[x].isNull = firstYear.isNull();
      if (!blendBlobs[x].isNull) {
        blendBlobs[x].firsty = xlate(firstYear.val());
        blendBlobs[x].secondy = xlate(secondYear.val());
      }
#endif
    }
    drawLevels (val, y, xlate(0), station->isCurrent
#ifdef blendingTest
		, blendBlobs
#endif
    );
  }

  // Draw depth lines later?
  if (tl != 'n')
    drawDepth (ymax, ymin, valmax, valmin, lineStep, labelWidth, minDepth,
	       maxDepth);

  // Height axis.
  for (int depth = minDepth; depth <= maxDepth; depth += lineStep) {
    Dstr dlabel;
    makeDepthLabel (depth, lineStep, unitsDesc, dlabel);
    double adj = 0.0;
    if (oughtHeight() > 1)
      adj = oughtHeight() / 2.0 + fontHeight() - oughtHeight();
    rightJustifyStringS (labelRight, xlate(0.1*depth)-adj, dlabel);
  }

  // Relatively straightforward logic to figure the increment for the
  // time axis.  We stop at 1 day.
  unsigned timeStep = 1;
  unsigned doubleOughtWidth = stringWidth("00");
  if ((Global::hour * timeStep) / increment < doubleOughtWidth)
    timeStep = 2;
  if ((Global::hour * timeStep) / increment < doubleOughtWidth)
    timeStep = 3;
  if ((Global::hour * timeStep) / increment < doubleOughtWidth)
    timeStep = 4;
  if ((Global::hour * timeStep) / increment < doubleOughtWidth)
    timeStep = 6;
  if ((Global::hour * timeStep) / increment < doubleOughtWidth)
    timeStep = 12;
  if ((Global::hour * timeStep) / increment < doubleOughtWidth)
    timeStep = 24;

  // Do time axis.
  const Timestamp timeAxisStopTime (endTime + Global::hour * timeStep);
  Timestamp loopt = startTime;
  for (loopt.floorHour(station->timezone);
       loopt < timeAxisStopTime;
       loopt.nextHour(station->timezone)) {
    if (loopt.tmStruct(station->timezone).tm_hour % timeStep == 0) {
      const double x = (loopt - startTime) / increment;
      drawHourTick (x, Colors::foreground, false);
      Dstr ts;
      loopt.printHour (ts, station->timezone);
      labelHourTick (x, ts);
    }
  }

  /* Make tick marks for day boundaries thicker. */
  /* They are not guaranteed to coincide with hour transitions! */
  loopt = startTime;
  for (loopt.floorDay(station->timezone);
       loopt < timeAxisStopTime;
       loopt.nextDay(station->timezone)) {
    const double x = (loopt - startTime) / increment;
    drawHourTick (x, Colors::foreground, true);
  }

  if (_style == clock) {

    // Write current time
    Dstr ts;
    currentTime.printTime (ts, station->timezone);
    centerStringOnLineSx (_xSize/2.0, 0, ts);

    // Write next max
    centerStringOnLineSx (_xSize/2.0, 1, nextMax.longDescription());
    nextMax.eventTime.printTime (ts, station->timezone);
    centerStringOnLineSx (_xSize/2.0, 2, ts);

    // Write next min
    centerStringOnLineSx (_xSize/2.0, -3, nextMin.longDescription());
    nextMin.eventTime.printTime (ts, station->timezone);
    centerStringOnLineSx (_xSize/2.0, -2, ts);

  } else {

    drawTitleLine (station->name);

    // Put timestamps for timestampable events.
    SafeVector<EventBlurb> topBlurbs, bottomBlurbs;
    EventBlurb tempBlurb;
    for (TideEventsIterator it = organizer.begin();
         it != organizer.end();
         ++it) {
      TideEvent &te = it->second;
      tempBlurb.x = (te.eventTime - startTime) / increment;
      switch (te.eventType) {
      case TideEvent::max:
      case TideEvent::min:
	te.eventTime.printDate (tempBlurb.line1, station->timezone);
	te.eventTime.printTime (tempBlurb.line2, station->timezone);
        measureBlurb (tempBlurb);
        topBlurbs.push_back (tempBlurb);
	break;
      case TideEvent::moonrise:
      case TideEvent::moonset:
      case TideEvent::slackrise:
      case TideEvent::slackfall:
      case TideEvent::markrise:
      case TideEvent::markfall:
      case TideEvent::newmoon:
      case TideEvent::firstquarter:
      case TideEvent::fullmoon:
      case TideEvent::lastquarter:
        drawHourTick (tempBlurb.x, Colors::mark, false);
	te.eventTime.printTime (tempBlurb.line2, station->timezone);
        tempBlurb.line1 = te.longDescription();
        if (!isBanner())
          if (stringWidth(tempBlurb.line1) > stringWidth(tempBlurb.line2))
            tempBlurb.line1 = te.shortDescription();
        measureBlurb (tempBlurb);
        bottomBlurbs.push_back (tempBlurb);
	break;
      default:
        break;
      }
    }

    drawBlurbs (1, topBlurbs);
    drawBlurbs (-3, bottomBlurbs);
  }

  // Extra lines.
  if (!station->markLevel.isNull()) {
    const double ytide = xlate(station->markLevel.val());
    drawHorizontalLinePxSy (labelWidth, _xSize-1, ytide, Colors::mark);
  }
  if (Global::settings["el"].c != 'n') {
    drawHorizontalLinePxSy (labelWidth, _xSize-1, xlate(0), Colors::datum);
    const double ytide = (ymax + ymin) / 2.0;
    drawHorizontalLinePxSy (labelWidth, _xSize-1, ytide, Colors::msl);
  }

  // X marks the current time.
  if (currentTime >= startTime && currentTime < endTime) {
    const double ytide = xlate(station->predictTideLevel(currentTime).val());
    drawX ((currentTime - startTime) / increment, ytide);
  }
}


// This must agree with whatever centerString does about odd numbers.
// However, it doesn't hurt to fudge by an even number to create a
// margin on both sides.
void Graph::measureBlurb (EventBlurb &blurb) const {
  int width (std::max (stringWidth (blurb.line1), stringWidth (blurb.line2))
	     + blurbMargin());
  blurb.deltaLeft = -width / 2;
  blurb.deltaRight = width + blurb.deltaLeft - 1;
}


const int Graph::blurbMargin() const {
  return 2;
}


void Graph::centerStringOnLineSx (double x, int line, const Dstr &s) {
  int y;
  if (line >= 0)
    y = line * fontHeight();
  else
    y = _ySize+fontHeight()*line-hourTickLen()-hourTickVerticalMargin();
  centerStringSxPy (x, y, s);
}


const unsigned Graph::depthLabelLeftMargin() const {
  return 2;
}


const unsigned Graph::depthLabelRightMargin() const {
  return 2;
}


const unsigned Graph::depthLineVerticalMargin() const {
  return 2;
}


const unsigned Graph::hourTickLen() const {
  return 8;
}


const unsigned Graph::hourTickVerticalMargin() const {
  return 2;
}


const double Graph::aspectFudgeFactor() const {
  return 1.0;
}


const unsigned Graph::startPosition (unsigned labelWidth) const {
  return labelWidth + 8;
}


void Graph::drawHourTick (double x, Colors::Colorchoice c, bool thick) {
  if (thick)
    drawBoxS (x-1.5, x+1.5, _ySize, _ySize-hourTickLen(), c);
  else
    drawVerticalLineS (x, _ySize, _ySize-hourTickLen(), c);
}


void Graph::labelHourTick (double x, const Dstr &label) {
  centerStringOnLineSx (x, -1, label);
}


void Graph::drawTitleLine (const Dstr &title) {
  centerStringOnLineSx (_xSize/2.0, 0, title);
}


void Graph::labelEvent (int topLine, const EventBlurb &blurb) {
  // This needs to get overridden in Banner.
  centerStringOnLineSx (blurb.x, topLine, blurb.line1);
  centerStringOnLineSx (blurb.x, topLine+1, blurb.line2);
}


const bool Graph::isBanner() const {
  return false;
}

}
