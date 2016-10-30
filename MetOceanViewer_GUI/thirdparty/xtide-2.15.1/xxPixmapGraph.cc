// $Id: xxPixmapGraph.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  xxPixmapGraph  Graph implemented as Pixmap.

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

#include "xtide.hh"
#include "libxtide/Graph.hh"
#include "libxtide/PixelatedGraph.hh"
#include "libxtide/RGBGraph.hh"
#include "xxPixmapGraph.hh"


xxPixmapGraph::xxPixmapGraph (unsigned xSize,
                              unsigned ySize,
                              GraphStyle style):
  PixelatedGraph (xSize, ySize, style) {
  assert (xSize >= Global::minGraphWidth && ySize >= Global::minGraphHeight);
  pixmap = xxX::makePixmap (xSize, ySize);
}


xxPixmapGraph::~xxPixmapGraph() {
  XFreePixmap (xxX::display, pixmap);
}


const unsigned xxPixmapGraph::stringWidth (const Dstr &s) const {
  return libxtide::stringWidth (Global::graphFont, s);
}


const unsigned xxPixmapGraph::fontHeight() const {
  return Global::graphFont.height;
}


const unsigned xxPixmapGraph::oughtHeight() const {
  return Global::graphFont.oughtHeight;
}


const unsigned xxPixmapGraph::oughtVerticalMargin() const {
  return 1;
}


void xxPixmapGraph::setPixel (int x, int y, Colors::Colorchoice c) {
  if (x < 0 || x >= (int)_xSize || y < 0 || y >= (int)_ySize)
    return;
  assert (c < (int)Colors::numColors);
  XSetForeground (xxX::display, xxX::spareGC, xxX::pixels[c]);
  XDrawPoint (xxX::display, pixmap, xxX::spareGC, x, y);
}


void xxPixmapGraph::drawVerticalLineP (int x, int y1, int y2,
				       Colors::Colorchoice c,
				       double opacity) {
  assert (c < (int)Colors::numColors);
  if (opacity >= 0.5) {
    XSetForeground (xxX::display, xxX::spareGC, xxX::pixels[c]);
    XDrawLine (xxX::display, pixmap, xxX::spareGC, x, y1, x, y2);
  }
}


void xxPixmapGraph::drawHorizontalLineP (int xlo, int xhi, int y,
					 Colors::Colorchoice c) {
  assert (c < (int)Colors::numColors);
  XSetForeground (xxX::display, xxX::spareGC, xxX::pixels[c]);
  if (xlo <= xhi) // Constraint per Graph.hh
    XDrawLine (xxX::display, pixmap, xxX::spareGC, xlo, y, xhi, y);
}


// libXft 2.2.0 exhibited background color anomalies and text alignment /
// typesetting anomalies on PseudoColor visuals.

void xxPixmapGraph::drawStringP (int x, int y, const Dstr &s) {
  for (unsigned a=0; a<s.length(); ++a) {
    const ClientSide::Glyph &g (Global::graphFont.glyphs[(uint8_t)s[a]]);
    for (SafeVector<ClientSide::Pixel>::const_iterator it (g.pixels.begin());
	 it != g.pixels.end(); ++it)
      if (it->opacity >= 90)  // Ideal threshold depends on the font.
        setPixel (x+it->x, y+it->y, Colors::foreground);
    x += g.advance;
  }
}
