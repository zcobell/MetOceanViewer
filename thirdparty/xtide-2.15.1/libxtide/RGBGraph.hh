// $Id: RGBGraph.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  RGBGraph  Graph implemented as raw RGB image.

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

class RGBGraph: public PixelatedGraph {
public:
  RGBGraph (unsigned xSize, unsigned ySize, GraphStyle style = normal);

  void writeAsPNG (png_rw_ptr write_data_fn);

  // xSize * ySize pixels, row major, starting at upper left.
  // One pixel = 8 bits R, 8 bits G, 8 bits B.
  // Same as a raw PPM, without the header.
  SafeVector<unsigned char> rgb;

protected:

  // The constructor sets these to the parsed equivalents of the
  // colors indicated in settings.  RGBGraphs have fleeting lifespans,
  // so there is no issue about keeping these in synch with changing
  // settings.
  uint8_t cmap[Colors::numColors][3]; // Wants to be [Colors::Colorchoice][3]

  // Overridden virtual methods.
  const unsigned stringWidth (const Dstr &s) const;
  const unsigned fontHeight() const;
  const unsigned oughtHeight() const;
  const unsigned oughtVerticalMargin() const;
  void drawStringP (int x, int y, const Dstr &s);
  void setPixel (int x, int y, Colors::Colorchoice c);
  void setPixel (int x,
                 int y,
                 Colors::Colorchoice c,
                 double opacity);
};

}
