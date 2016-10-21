// $Id: SVGGraph.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  SVGGraph  Graph implemented as Scalable Vector Graphics (SVG).

    Copyright (C) 2010 Jack Greenbaum and David Flater.

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

class SVGGraph: public Graph {
public:
  SVGGraph (unsigned xSize, unsigned ySize, GraphStyle style = normal);

  void print (Dstr &text_out);

protected:

  Dstr SVG;

  const unsigned stringWidth (const Dstr &s) const;
  const unsigned fontHeight() const;
  const unsigned oughtHeight() const;
  const unsigned oughtVerticalMargin() const;

  void drawLevels (const SafeVector<double> &val,
		   const SafeVector<double> &y,
		   double yzulu,
		   bool isCurrent
#ifdef blendingTest
		   , const SafeVector<BlendBlob> &blendBlobs
#endif
		   );

  void drawX (double x, double y);
  void drawBoxS (double x1, double x2, double y1, double y2,
		 Colors::Colorchoice c);
  void drawVerticalLineS (double x, double y1, double y2,
			  Colors::Colorchoice c);
  void drawHorizontalLineS (double xlo, double xhi, double y,
			    Colors::Colorchoice c);
  void drawHorizontalLinePxSy (int xlo, int xhi, double y,
			       Colors::Colorchoice c);
  void centerStringSxPy    (double x, int y, const Dstr &s);
  void rightJustifyStringS (double x, double y, const Dstr &s);
};

}
