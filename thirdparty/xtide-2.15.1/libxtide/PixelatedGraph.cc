// $Id: PixelatedGraph.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  PixelatedGraph  Graphs that do not use scalable vectors.

    Copyright (C) 2010  David Flater.

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

namespace libxtide {


// In drawing of line graphs, slope at which to abandon the thick line
// drawing algorithm.
static const double slopeLimit (5.0);


PixelatedGraph::PixelatedGraph (unsigned xSize, unsigned ySize,
				GraphStyle style):
  Graph (xSize, ySize, style) {
}


void PixelatedGraph::setPixel (int x, int y,
			       Colors::Colorchoice c, double opacity) {
  assert (c < (int)Colors::numColors);
  if (opacity >= 0.5)
    setPixel (x, y, c);
}


void PixelatedGraph::drawVerticalLineP (int x, int y1, int y2,
					Colors::Colorchoice c,
					double opacity) {
  int ylo, yhi;
  if (y1 < y2) {
    ylo = y1; yhi = y2;
  } else {
    ylo = y2; yhi = y1;
  }
  if (opacity == 1)
    for (int i=ylo; i<=yhi; ++i)
      setPixel (x, i, c);
  else
    for (int i=ylo; i<=yhi; ++i)
      setPixel (x, i, c, opacity);
}


void PixelatedGraph::drawVerticalLinePxSy (int x, double y1, double y2,
					   Colors::Colorchoice c,
					   double opacity) {
  double ylo, yhi;
  if (y1 < y2) {
    ylo = y1; yhi = y2;
  } else {
    ylo = y2; yhi = y1;
  }
  int ylo2 ((int) ceil (ylo));
  int yhi2 ((int) floor (yhi));
  if (ylo2 < yhi2)
    drawVerticalLineP (x, ylo2, yhi2-1, c, opacity);
  // What if they both fall within the same pixel:  ylo2 > yhi2
  if (ylo2 > yhi2) {
    assert (yhi2 == ylo2 - 1);
    setPixel (x, yhi2, c, opacity * (yhi - ylo));
  } else {
    // The normal case.
    if (ylo < ylo2)
      setPixel (x, ylo2-1, c, opacity * (ylo2 - ylo));
    if (yhi > yhi2)
      setPixel (x, yhi2, c, opacity * (yhi - yhi2));
  }
}


void PixelatedGraph::drawVerticalLineS (double x, double y1, double y2,
					Colors::Colorchoice c) {
  drawVerticalLinePxSy (Global::ifloor (x), y1, y2, c);
}


void PixelatedGraph::drawFunkyLine (double prevytide,
				    double ytide,
				    double nextytide,
				    int x,
				    Colors::Colorchoice c) {
  double dy, yleft, yright;
  double slw (Global::settings["lw"].d);

  // The fix for line slope breaks down when the slope gets nasty, so
  // switch to a more conservative strategy when that happens.  Line
  // width becomes 1 no matter what.

#define dohalfline(yy) {                                          \
  double lw;                                                      \
  if (fabs(dy) < slopeLimit)                                      \
    lw = (1.0 + (M_SQRT2 - 1.0) * fabs(dy)) * slw / 2.0;          \
  else                                                            \
    lw = (fabs(dy) + slw) / 2.0;                                  \
  if (dy < 0.0)                                                   \
    lw = -lw;                                                     \
  yy = ytide - lw;                                                \
}

  dy = ytide - prevytide;
  dohalfline (yleft);
  dy = ytide - nextytide;
  dohalfline (yright);

  // Fix degenerate cases.
  if (ytide > yleft && ytide > yright) {
    if (yleft > yright)
      yleft = ytide + slw / 2.0;
    else
      yright = ytide + slw / 2.0;
  } else if (ytide < yleft && ytide < yright) {
    if (yleft < yright)
      yleft = ytide - slw / 2.0;
    else
      yright = ytide - slw / 2.0;
  }
  drawVerticalLinePxSy (x, yleft, yright, c);
}


void PixelatedGraph::drawX (double x, double y) {
  int ix = Global::ifloor (x);
  int iy = Global::ifloor (y);
  drawVerticalLineP   (ix, iy-4, iy+4, Colors::foreground);
  drawHorizontalLineP (ix-4, ix+4, iy, Colors::foreground);
}


void PixelatedGraph::drawLevels (const SafeVector<double> &val,
				 const SafeVector<double> &y,
				 double yzulu,
				 bool isCurrent
#ifdef blendingTest
				 , const SafeVector<BlendBlob> &blendBlobs
#endif
				 ) {
  const char gs (Global::settings["gs"].c);
  const double opacity (gs == 's' ? Global::settings["to"].d : 1.0);

  // Harmonize this with the quantized y coordinate of the 0 kt line to avoid
  // anomalies like a gap between the flood curve and the line.
  yzulu = Global::ifloor(yzulu);

  for (int x=0; x<(int)_xSize; ++x) {

    // Coloration is determined from the predicted heights, not from
    // the eventTypes of surrounding tide events.  Ideally the two
    // would never disagree, but for pathological sub stations they
    // can.
    if (isCurrent) {
      Colors::Colorchoice c = (val[x+1] > 0.0 ? Colors::flood : Colors::ebb);
      switch (gs) {
      case 'd':
        drawVerticalLinePxSy (x, yzulu, y[x+1], c, opacity);
	break;
      case 'l':
        drawFunkyLine (y[x], y[x+1], y[x+2], x, c);
	break;
      case 's':
        drawVerticalLinePxSy (x, yzulu, y[x+1], c, opacity);
        drawFunkyLine (y[x], y[x+1], y[x+2], x, Colors::foreground);
	break;
      default:
        assert (false);
      }
    } else {
      Colors::Colorchoice c = (val[x] < val[x+1] ? Colors::flood : Colors::ebb);
      switch (gs) {
      case 'd':
        drawVerticalLinePxSy (x, _ySize, y[x+1], c, opacity);
	break;
      case 'l':
        drawFunkyLine (y[x], y[x+1], y[x+2], x, c);
	break;
      case 's':
        drawVerticalLinePxSy (x, _ySize, y[x+1], c, opacity);
        drawFunkyLine (y[x], y[x+1], y[x+2], x, Colors::foreground);
	break;
      default:
        assert (false);
      }
    }

#ifdef blendingTest
    if (!blendBlobs[x+1].isNull) {
      setPixel (x, (int)blendBlobs[x+1].firsty, Colors::mark);
      setPixel (x, (int)blendBlobs[x+1].secondy, Colors::msl);
    }
#endif
  }
}


void PixelatedGraph::drawBoxS (double x1, double x2, double y1, double y2,
Colors::Colorchoice c) {
  int ix1 (Global::ifloor (x1)), ix2 (Global::ifloor (x2));
  if (ix1 > ix2)
    std::swap (ix1, ix2);
  // Exclude upper limit so adjacent boxes don't overlap.
  for (int x=ix1; x<ix2; ++x)
    drawVerticalLinePxSy (x, y1, y2, c);
}


void PixelatedGraph::drawHorizontalLinePxSy (int xlo, int xhi, double y,
					     Colors::Colorchoice c) {
  drawHorizontalLineP (xlo, xhi, Global::ifloor(y), c);
}


void PixelatedGraph::drawHorizontalLineP (int xlo, int xhi, int y,
					  Colors::Colorchoice c) {
  for (int i=xlo; i<=xhi; ++i)
    setPixel (i, y, c);
}


// x quantization has to happen first to avoid an off-by-one error.  It is
// most obvious in TTYGraph where the effective font size is 1 pixel.  For
// example,
//   Let x = 1.1
//   Hour tick gets drawn at floor(1.1) = column 1
//   Label of length 1 gets drawn at floor(1.1 - 0.5) = column 0
// So you get
//   8
//    |
// Duh.
void PixelatedGraph::centerStringSxPy (double x, int y, const Dstr &s) {
  // int cast needed to prevent surprising promotion to unsigned
  drawStringP (Global::ifloor(x)-(int)stringWidth(s)/2, y, s);
}


void PixelatedGraph::rightJustifyStringS (double x, double y, const Dstr &s) {
  drawStringP (Global::ifloor(x-stringWidth(s)), Global::ifloor(y), s);
}

}
