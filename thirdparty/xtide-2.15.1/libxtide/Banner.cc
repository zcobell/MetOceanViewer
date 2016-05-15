// $Id: Banner.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  Banner  Graph printed sideways on tractor feed dot matrix printer

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
#include "PixelatedGraph.hh"
#include "TTYGraph.hh"
#include "Banner.hh"

namespace libxtide {


// Fudge factor to correct aspect ratio on line printer
// Correct 10x6 of Pica type to be 1:1
static const double LPaspectfudge (0.6);


Banner::Banner (unsigned xSize, unsigned ySize): TTYGraph (ySize, xSize) {
  //                                         turn sideways ^^^^^^^^^^^^
}


Banner * const Banner::factory (const Station &station,
				unsigned xSize,
				Timestamp startTime,
				Timestamp endTime) {
  // Everything is sideways.
  Interval increment (std::max (
    (interval_rep_t)1,
    Global::intervalround (Global::aspectMagicNumber /
                           (double)xSize /
                           (station.aspect * LPaspectfudge))));

  // I really painted myself into a corner this time.  To create a
  // Banner I need to know the length.  The length I want depends on
  // labelWidth (the amount by which Graph fudges the start position
  // to avoid clobbering it with depth labels).  To get labelWidth I
  // need an instance of Banner.  Catch-22.

  // In the absence of a better idea, I create one Banner just to get
  // labelWidth and then do it right.  Thankfully, banners are not the
  // sort of objects that get created often.
  Banner throwawayBanner (xSize, Global::minTTYwidth);

  // Stuff duplicated from Graph::drawTides, but with confusion of X and Y.
  const double ymin (vertGraphMargin * (double)xSize);
  const double ymax ((double)xSize - ymin);
  const double valmin (station.minLevelHeuristic().val());
  const double valmax (station.maxLevelHeuristic().val());
  assert (valmin < valmax);

  unsigned lineStep, labelWidth, labelRight;
  int minDepth, maxDepth;
  const Dstr unitsDesc (Units::shortName (station.predictUnits()));
  throwawayBanner.figureLabels (ymax, ymin, valmax, valmin, unitsDesc,
				lineStep, labelWidth,
				labelRight, minDepth, maxDepth);

  // At last we have labelWidth and can proceed.
  unsigned ySize (std::max (Global::iround ((endTime - startTime) / increment +
				    throwawayBanner.startPosition(labelWidth)),
			    (int)Global::minTTYwidth));
  return new Banner (xSize, ySize);
}


void Banner::drawHorizontalLineP (int xlo, int xhi, int y,
			         Colors::Colorchoice c unusedParameter) {
  const char bar (VT100_mode ? 'ø' : '|');
  for (int i=xlo; i<=xhi; ++i)
    setPixel (i, y, bar);
}


void Banner::drawHourTick (double x, Colors::Colorchoice c unusedParameter,
bool thick) {
  int ix = Global::ifloor(x);
  setPixel (ix, _ySize-1, '-');
  if (thick) {
    setPixel (ix-1, _ySize-1, '-');
    setPixel (ix+1, _ySize-1, '-');
  }
}


void Banner::print (Dstr &text_out) {
  // Everything is sideways.
  text_out = (char *)NULL;
  SafeVector<char> buf (_ySize+2);
  buf[_ySize]   = '\n';
  buf[_ySize+1] = '\0';
  for (unsigned x=0; x<_xSize; ++x) {
    for (unsigned y=0; y<_ySize; ++y)
      buf[y] = tty[(_ySize-1-y)*_xSize+x];
    text_out += &(buf[0]);
  }
  if (VT100_mode)
    VT100_postproc (text_out);
}


const double Banner::aspectFudgeFactor() const {
  return LPaspectfudge;
}


void Banner::drawStringSideways (int x, int y, const Dstr &s) {
  for (unsigned a=0; a<s.length(); ++a)
    setPixel (x, y-a, s[a]);
}


void Banner::labelHourTick (double x, const Dstr &label) {
  drawStringSideways (Global::ifloor(x),
		      _ySize - hourTickLen() - 1,
		      label);
}


void Banner::labelEvent (int topLine, const EventBlurb &blurb) {
  int x = Global::ifloor (blurb.x);
  if (topLine >= 0) {
    drawStringSideways (x-1, blurb.line1.length()-1, blurb.line1);
    drawStringSideways (x,   blurb.line2.length()-1, blurb.line2);
  } else {
    int y (_ySize - hourTickLen() - 4);
    drawStringSideways (x-1, y, blurb.line1);
    drawStringSideways (x,   y, blurb.line2);
  }
}


// This has to agree with labelEvent.
void Banner::measureBlurb (EventBlurb &blurb) const {
  blurb.deltaLeft  = -1;
  blurb.deltaRight = 0;
}


void Banner::drawTitleLine (const Dstr &title unusedParameter) {
  // Lose it.
}


const bool Banner::isBanner() const {
  return true;
}

}
