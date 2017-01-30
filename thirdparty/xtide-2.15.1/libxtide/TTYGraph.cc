// $Id: TTYGraph.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  TTYGraph  Graph implemented on dumb terminal

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

namespace libxtide {


// Fudge factor to correct aspect ratio on TTY
// Correct 80x24 of VT100 to match 4/3 aspect
static const double TTYaspectfudge = 2.5;


TTYGraph::TTYGraph (unsigned xSize, unsigned ySize, GraphStyle style):
  PixelatedGraph (xSize, ySize, style),
  VT100_mode (Global::codeset == "VT100") {
  assert (xSize >= Global::minTTYwidth && ySize >= Global::minTTYheight);
  tty.resize (xSize * ySize);
}


const unsigned TTYGraph::stringWidth (const Dstr &s) const {
  return s.length();
}


const unsigned TTYGraph::fontHeight() const {
  return 1;
}


const unsigned TTYGraph::oughtHeight() const {
  return 1;
}


const unsigned TTYGraph::oughtVerticalMargin() const {
  return 0;
}


const unsigned TTYGraph::hourTickLen() const {
  return 1;
}


const unsigned TTYGraph::hourTickVerticalMargin() const {
  return 0;
}


const unsigned TTYGraph::depthLabelLeftMargin() const {
  return 0;
}


const unsigned TTYGraph::depthLabelRightMargin() const {
  return 1;
}


const unsigned TTYGraph::depthLineVerticalMargin() const {
  return 0;
}


const double TTYGraph::aspectFudgeFactor() const {
  return TTYaspectfudge;
}


const unsigned TTYGraph::startPosition (unsigned labelWidth) const {
  return labelWidth;
}


const int TTYGraph::blurbMargin() const {
  return 0;
}


void TTYGraph::setPixel (int x, int y, Colors::Colorchoice c) {
  assert (c < (int)Colors::numColors);
  char color_analog;
  switch (c) {
  case Colors::daytime:
    color_analog = ' ';
    break;
  case Colors::nighttime:
    color_analog = (VT100_mode ? 'þ' : '·');
    break;
  default:
    color_analog = '*';
  }
  setPixel (x, y, color_analog);
}


void TTYGraph::setPixel (int x, int y, char c) {
  if (x < 0 || x >= (int)_xSize || y < 0 || y >= (int)_ySize)
    return;
  tty[y * _xSize + x] = c;
}


void TTYGraph::drawHorizontalLineP (int xlo, int xhi, int y,
				    Colors::Colorchoice c unusedParameter) {
  for (int i=xlo; i<=xhi; ++i)
    setPixel (i, y, '-');
}


void TTYGraph::drawHorizontalLinePxSy (int xlo, int xhi, double y,
				       Colors::Colorchoice c) {
  if (!VT100_mode || isBanner())
    PixelatedGraph::drawHorizontalLinePxSy (xlo, xhi, y, c);
  else {
    // VT100 glyphs are 10 pixels high.  Special Graphics characters o-s are
    // horizontal lines at scan lines 0,2,4,6,8, so the y ranges that they
    // cover are 0.05, 0.25, 0.45, 0.65, 0.85 ± 0.1.  That means that the
    // upper 5% wants to go on the next line of text, which could mess things
    // up.  The following is just wrong 5% of the time.
    const int yi (Global::ifloor (y));
    const char lineChar (0x80 | ('o' + std::min (4U,
 	                 (unsigned)Global::iround((y-floor(y)-0.05)*5))));
    for (int i=xlo; i<=xhi; ++i)
      setPixel (i, yi, lineChar);
  }
}


void TTYGraph::drawHourTick (double x, Colors::Colorchoice c unusedParameter,
bool thick) {
  int ix = Global::ifloor(x);
  setPixel (ix, _ySize-1, '|');
  if (thick) {
    setPixel (ix-1, _ySize-1, '|');
    setPixel (ix+1, _ySize-1, '|');
  }
}


void TTYGraph::drawStringP (int x, int y, const Dstr &s) {
  for (unsigned a=0; a<s.length(); ++a)
    setPixel (x+a, y, s[a]);
}


// "In" is US-ASCII (G0).  "Out" is Special Graphics (G1).  The high bit is
// used to indicate characters from the Special Graphics set.  Special
// Graphics and US-ASCII differ only in the range 95-127, so it is not
// necessary to shift all of the time.  (N.B., xterm draws Special Graphics
// character 95 as underscore, same as US-ASCII, but it's supposed to be a
// blank.)
void TTYGraph::VT100_postproc (Dstr &text) {
  Dstr text_out;
  // Banners get the SCS in the text_boilerplate; graphs don't.
  if (!isBanner())
    text_out = Global::VT100_init;
  const char SI ('\017'), SO ('\016');
  bool shifted (false);
  const unsigned l (text.length());
  for (unsigned i=0; i<l; ++i) {
    const char c8    (text[i]);
    const bool hibit (c8 & 0x80);
    const char c7    (c8 & 0x7F);
    if (c7 >= '_') {
      if (hibit && !shifted) {
	text_out += SO;
	shifted = true;
      } else if (!hibit && shifted) {
	text_out += SI;
	shifted = false;
      }
    }
    text_out += c7;
  }
  if (shifted)
    text_out += SI;  // Be kind, rewind.
  text = text_out;
}


void TTYGraph::print (Dstr &text_out) {
  text_out = (char *)NULL;
  SafeVector<char> lineBuf (_xSize+2);
  lineBuf[_xSize]   = '\n';
  lineBuf[_xSize+1] = '\0';
  for (SafeVector<char>::const_iterator it (tty.begin());
       it != tty.end();
       it += _xSize) {
    std::copy (it, it+_xSize, lineBuf.begin());
    text_out += &(lineBuf[0]);
  }
  if (VT100_mode)
    VT100_postproc (text_out);
  else
    Global::finalizeCodeset (text_out, Global::codeset, Format::text);
}


void TTYGraph::drawX (double x, double y) {
  setPixel (Global::ifloor(x), Global::ifloor(y), '+');
}

}
