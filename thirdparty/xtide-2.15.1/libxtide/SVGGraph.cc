// $Id: SVGGraph.cc 5748 2014-10-11 19:38:53Z flaterco $

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

#include "libxtide.hh"
#include "Graph.hh"
#include "SVGGraph.hh"

namespace libxtide {


// 2011-08-14
// With no font metrics, no safe fonts, and no portable embedding mechanism,
// this is the best I can do.  Liberation Mono is for Linux; Courier New is
// for Windows.  AFAIK this is the only pair of monospaced fonts that covers
// Windows and Linux and is metric-compatible--which is unfortunate, because
// Courier New is too light.

// 2012-02-14
// In Xft, LiberationMono-10 and -11 have advance = 8, but that is clearly
// too much for whatever Firefox is doing.  And 7 is not quite enough.

static const char     SVGfontName[]         = "Liberation Mono, Courier New";
static const unsigned SVGfontGlyphWidth     = 7U;
static const unsigned SVGfontGlyphHeight    = 12U;
static const unsigned SVGfontOughtHeight    = 9U;
static const double   SVGfontBaselineAdjust = 12U;


SVGGraph::SVGGraph (unsigned xSize,
		    unsigned ySize,
		    GraphStyle style):
  Graph (xSize, ySize, style) {
  assert (xSize >= Global::minGraphWidth && ySize >= Global::minGraphHeight);
  const char gs (Global::settings["gs"].c);

  SVG += "<?xml version=\"1.0\" encoding=\"iso-8859-1\" standalone=\"no\"?>\n";
  SVG += "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";

  SVG += "<svg width=\"";
  SVG += _xSize;
  SVG += "px\" height=\"";
  SVG += _ySize;
  SVG += "px\" viewBox=\"0 0 ";
  SVG += _xSize;
  SVG += ' ';
  SVG += _ySize;
  SVG += "\" xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";

  // Translate color palette into SVG styles.
  SVG += "<defs>\n\t<style type=\"text/css\">\n\t\t<![CDATA[\n";
  for (unsigned a=0; a<Colors::numColors; ++a) {
    uint8_t r, g, b;
    Colors::parseColor (Global::settings[Colors::colorarg[a]].s, r, g, b);
    char SVGcolorSpec[8];
    sprintf (SVGcolorSpec, "#%02x%02x%02x", r, g, b);
    SVG += "\t\t\t.";
    SVG += Colors::colorarg[a];
    SVG += " {";
    if (gs == 's' && (a == Colors::flood || a == Colors::ebb)) {
      SVG += "opacity:";
      SVG += Global::settings["to"].d;
      SVG += ";";
    }
    SVG += "fill:";
    SVG += SVGcolorSpec;
    SVG += ";stroke:";
    SVG += SVGcolorSpec;
    SVG += "}\n";
    SVG += "\t\t\t.";
    SVG += Colors::colorarg[a];
    SVG += "nf {";
    if (gs == 's' && (a == Colors::flood || a == Colors::ebb)) {
      SVG += "opacity:";
      SVG += Global::settings["to"].d;
      SVG += ";";
    }
    SVG += "fill:none;stroke:";
    SVG += SVGcolorSpec;
    SVG += "}\n";
  }
  SVG += "\t\t]]>\n\t</style>\n</defs>\n";

  // Establish clip box so labels at the edges don't run past the graphics.
  SVG += "<defs>\n\t<clipPath id=\"clipBox\">\n\t\t<rect x=\"0\" y=\"0\" width=\"";
  SVG += _xSize;
  SVG += "\" height=\"";
  SVG += _ySize;
  SVG += "\"/>\n\t</clipPath>\n</defs>\n<g clip-path=\"url(#clipBox)\" font-family=\"";
  SVG += SVGfontName;
  SVG += "\" font-size=\"";
  SVG += SVGfontGlyphHeight;
  SVG += "\">\n";
}


const unsigned SVGGraph::stringWidth (const Dstr &s) const {
  return s.length() * SVGfontGlyphWidth;
}


const unsigned SVGGraph::fontHeight() const {
  return SVGfontGlyphHeight;
}


const unsigned SVGGraph::oughtHeight() const {
  return SVGfontOughtHeight;
}


const unsigned SVGGraph::oughtVerticalMargin() const {
  return 1;
}


void SVGGraph::drawX (double x, double y) {
  drawVerticalLineS   (x, y-4, y+4, Colors::foreground);
  drawHorizontalLineS (x-4, x+4, y, Colors::foreground);
}


void SVGGraph::drawBoxS (double x1, double x2, double y1, double y2,
			 Colors::Colorchoice c) {
  if (x1 > x2)
    std::swap (x1, x2);
  if (y1 > y2)
    std::swap (y1, y2);
  SVG += "<rect stroke-width=\"0\" x=\"";
  SVG += x1;
  SVG += "\" y=\"";
  SVG += y1;
  SVG += "\" width=\"";
  SVG += x2-x1;
  SVG += "\" height=\"";
  SVG += y2-y1;
  SVG += "\" class=\"";
  SVG += Colors::colorarg[c];
  SVG += "\"/>\n";
}


void SVGGraph::drawVerticalLineS (double x, double y1, double y2,
				  Colors::Colorchoice c) {
  SVG += "<line x1=\"";
  SVG += x;
  SVG += "\" y1=\"";
  SVG += y1;
  SVG += "\" x2=\"";
  SVG += x;
  SVG += "\" y2=\"";
  SVG += y2;
  SVG += "\" class=\"";
  SVG += Colors::colorarg[c];
  SVG += "\" stroke-width=\"1\"/>\n";
}


void SVGGraph::drawHorizontalLinePxSy (int xlo,
				       int xhi,
				       double y,
				       Colors::Colorchoice c) {
  if (xlo <= xhi) // Constraint per Graph::drawHorizontalLine.
    drawHorizontalLineS ((double)xlo, (double)xhi+1, y, c);
}


void SVGGraph::drawHorizontalLineS (double xlo, double xhi, double y,
				    Colors::Colorchoice c) {
  if (xlo <= xhi) { // Constraint per Graph::drawHorizontalLine.
    SVG += "<line x1=\"";
    SVG += xlo;
    SVG += "\" y1=\"";
    SVG += y;
    SVG += "\" x2=\"";
    SVG += xhi;
    SVG += "\" y2=\"";
    SVG += y;
    SVG += "\" class=\"";
    SVG += Colors::colorarg[c];
    SVG += "\" stroke-width=\"1\"/>\n";
  }
}


void SVGGraph::centerStringSxPy (double x, int y, const Dstr &s) {
  SVG += "<text x=\"";
  SVG += x;
  SVG += "\" y=\"";
  SVG += y + SVGfontBaselineAdjust;
  SVG += "\" text-anchor=\"middle\">";
  SVG += s;
  SVG += "</text>\n";
}


void SVGGraph::rightJustifyStringS (double x, double y, const Dstr &s) {
  SVG += "<text x=\"";
  SVG += x;
  SVG += "\" y=\"";
  SVG += y + SVGfontBaselineAdjust;
  SVG += "\" text-anchor=\"end\">";
  SVG += s;
  SVG += "</text>\n";
}


void SVGGraph::print (Dstr &text_out) {
  SVG += "</g>\n</svg>\n";
  text_out = SVG;
}


void SVGGraph::drawLevels (const SafeVector<double> &val,
			   const SafeVector<double> &y,
			   double yzulu,
			   bool isCurrent
#ifdef blendingTest
			   , const SafeVector<BlendBlob> &blendBlobs
#endif
			   ) {

  // With graphstyle s, you get a semi-transparent filled curve topped with a
  // black line.  Otherwise, you get a filled curve or a multicolored line,
  // never both.
  const char gs (Global::settings["gs"].c);

  // Filled curve drawing block
  if (gs != 'l') {
    Colors::Colorchoice prevc=Colors::flood, c=Colors::flood;
    for (int x=0; x<=(int)_xSize; ++x) {
      prevc = c;
      if (isCurrent)
	c = (val[x+1] > 0.0 ? Colors::flood : Colors::ebb);
      else
	c = (val[x] < val[x+1] ? Colors::flood : Colors::ebb);
      if ((prevc != c) || (!x)) {
        // Start a new polygon, change colors.
	double xadj = x-1;
        // Don't cross yzulu with the wrong color.
	if (isCurrent && ((c == Colors::flood && y[x] > yzulu) ||
			  (c == Colors::ebb && y[x] < yzulu)))
	  xadj = linterp (xadj, (double)x, (yzulu-y[x])/(y[x+1]-y[x]));
        if (x) {
          SVG += ' ';
          SVG += xadj;
          SVG += ' ';
          if (isCurrent)
            SVG += yzulu;
          else
            SVG += _ySize;
          SVG += "\"/>\n";
        }
	SVG += "<polygon class=\"";
	SVG += Colors::colorarg[c];
	SVG += "\" stroke-width=\"0\" points=\"";
        if (isCurrent) {
          SVG += xadj;
          SVG += ' ';
          SVG += yzulu;
        } else {
          SVG += x-1;
          SVG += ' ';
          SVG += _ySize;
          SVG += ' ';
          SVG += x-1;
          SVG += ' ';
          SVG += y[x];
        }
      }
      SVG += ' ';
      SVG += x;
      SVG += ' ';
      SVG += y[x+1];
    }
    SVG += ' ';
    SVG += _xSize;
    SVG += ' ';
    if (isCurrent)
      SVG += yzulu;
    else
      SVG += _ySize;
    SVG += "\"/>\n";
  }

  // Line-drawing block
  if (gs != 'd') {
    Colors::Colorchoice prevc=Colors::foreground, c=Colors::foreground;
    const double lw (Global::settings["lw"].d);
    for (int x=0; x<=(int)_xSize+1; ++x) {
      if (gs == 'l') {
	prevc = c;
	if (isCurrent)
	  c = (val[x+1] > 0.0 ? Colors::flood : Colors::ebb);
	else
	  c = (val[x] < val[x+1] ? Colors::flood : Colors::ebb);
      }
      if ((prevc != c) || (!x)) {
        // Start a new line, change colors.
        double xadj = x-1;
        double yadj = y[x];
        // Don't cross yzulu with the wrong color.
        if (isCurrent && ((c == Colors::flood && yadj > yzulu) ||
                          (c == Colors::ebb && yadj < yzulu))) {
          xadj = linterp (xadj, (double)x, (yzulu-yadj)/(y[x+1]-yadj));
          yadj = yzulu;
        }
        if (x) {
          if (isCurrent) {
	    SVG += ' ';
	    SVG += xadj;
	    SVG += ' ';
	    SVG += yzulu;
          }
          SVG += "\"/>\n";
        }
	SVG += "<polyline class=\"";
	SVG += Colors::colorarg[c];
	SVG += "nf\" stroke-width=\"";
	SVG += lw;
	SVG += "\" points=\"";
        SVG += xadj;
        SVG += ' ';
	SVG += yadj;
      }
      SVG += ' ';
      SVG += x;
      SVG += ' ';
      SVG += y[x+1];
    }
    SVG += "\"/>\n";
  }

#ifdef blendingTest
  {
    Dstr firstLine, secondLine;
    for (int x=0; x<=(int)_xSize+2; ++x) {
      if (firstLine.isNull()) {
        if (!blendBlobs[x].isNull) {
	  firstLine += "<polyline class=\"";
	  firstLine += Colors::colorarg[Colors::mark];
	  firstLine += "nf\" stroke-width=\"0.5\" points=\"";
	  firstLine += x-1;
	  firstLine += ' ';
	  firstLine += blendBlobs[x].firsty;
	  secondLine += "<polyline class=\"";
	  secondLine += Colors::colorarg[Colors::msl];
	  secondLine += "nf\" stroke-width=\"0.5\" points=\"";
	  secondLine += x-1;
	  secondLine += ' ';
	  secondLine += blendBlobs[x].secondy;
        }
      } else {
        if (blendBlobs[x].isNull)
          break;
        else {
	  firstLine += ' ';
	  firstLine += x-1;
	  firstLine += ' ';
	  firstLine += blendBlobs[x].firsty;
	  secondLine += ' ';
	  secondLine += x-1;
	  secondLine += ' ';
	  secondLine += blendBlobs[x].secondy;
        }
      }
    }
    if (!firstLine.isNull()) {
      firstLine += "\"/>\n";
      secondLine += "\"/>\n";
      SVG += firstLine;
      SVG += secondLine;
    }
  }
#endif
}

}
