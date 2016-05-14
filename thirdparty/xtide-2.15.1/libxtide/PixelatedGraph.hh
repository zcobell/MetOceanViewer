// $Id: PixelatedGraph.hh 5748 2014-10-11 19:38:53Z flaterco $

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

namespace libxtide {

class PixelatedGraph: public Graph {
protected:

  PixelatedGraph (unsigned xSize, unsigned ySize, GraphStyle style = normal);

  void drawX (double x, double y);

  // This iterates pixelwise over drawVerticalLine or drawFunkyLine.
  void drawLevels (const SafeVector<double> &val,
		   const SafeVector<double> &y,
		   double yzulu,
		   bool isCurrent
#ifdef blendingTest
		   , const SafeVector<BlendBlob> &blendBlobs
#endif
		   );


  // Low-level drawing functions.

  // This implementation of drawBox transforms the x coordinates to ints and
  // iterates over drawVerticalLinePxSy.  The upper limit of x is excluded so
  // that adjacent boxes won't overlap.
  void drawBoxS (double x1, double x2, double y1, double y2,
		 Colors::Colorchoice c);

  // Vertical lines.
  // Line thickness is always 1.
  // Ordering of y1 and y2 is irrelevant.
  // Default implementations:
  //   S quantizes (floor) and calls PxSy.
  //   PxSy calls P and then calls setPixel with an opacity to anti-alias
  //     the endpoints.
  //   P just iterates over setPixel.
  void drawVerticalLineS (double x, double y1, double y2,
			  Colors::Colorchoice c);
  void drawVerticalLinePxSy (int x, double y1, double y2,
			     Colors::Colorchoice c,
			     double opacity = 1.0);
  virtual void drawVerticalLineP (int x, int y1, int y2,
				  Colors::Colorchoice c,
				  double opacity = 1.0);

  // Horizontal lines.  Line thickness is always 1.  No line will be drawn if
  // xlo > xhi.  As implemented, PxSy quantizes y and calls P, and P iterates
  // over setPixel.
  void drawHorizontalLinePxSy (int xlo, int xhi, double y,
			       Colors::Colorchoice c);
  virtual void drawHorizontalLineP (int xlo, int xhi, int y,
				    Colors::Colorchoice c);

  // Individual pixels.
  // The first just calls the second if opacity >= 0.5.
  // Override to do anti-aliasing.
  virtual void setPixel (int x, int y, Colors::Colorchoice c,
                         double opacity); // Opacity ranges from 0 to 1
  virtual void setPixel (int x, int y, Colors::Colorchoice c) = 0;

  // Text.
  void centerStringSxPy (double x, int y, const Dstr &s);
  void rightJustifyStringS (double x, double y, const Dstr &s);
  virtual void drawStringP (int x, int y, const Dstr &s) = 0;


private:
  // Horrible logic for line graphs.
  void drawFunkyLine (double prevytide,
                      double ytide,
                      double nextytide,
                      int x,
                      Colors::Colorchoice c);
};

}
