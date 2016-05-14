// $Id: xxPixmapGraph.hh 4303 2012-02-14 00:59:27Z flaterco $

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

class xxPixmapGraph: public PixelatedGraph {
public:
  xxPixmapGraph (unsigned xSize, unsigned ySize, GraphStyle style = normal);
  ~xxPixmapGraph();

  Pixmap pixmap;

protected:

  const unsigned stringWidth (const Dstr &s) const;
  const unsigned fontHeight() const;
  const unsigned oughtHeight() const;
  const unsigned oughtVerticalMargin() const;

  void drawStringP (int x, int y, const Dstr &s);

  // These override perfectly good versions in PixelatedGraph in order to use
  // the available X11 drawing functions.
  void drawVerticalLineP (int x, int y1, int y2, Colors::Colorchoice c,
			  double opacity = 1.0);
  void drawHorizontalLineP (int xlo, int xhi, int y, Colors::Colorchoice c);

  void setPixel (int x, int y, Colors::Colorchoice c);
};
