// $Id: TTYGraph.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  TTYGraph  Graph implemented on dumb terminal.

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

class TTYGraph: public PixelatedGraph {
public:
  TTYGraph (unsigned xSize, unsigned ySize, GraphStyle style = normal);

  void print (Dstr &text_out);

protected:

  // xSize * ySize characters, row major, starting at upper left.
  SafeVector<char> tty;

  // Overridden virtual methods.
  const unsigned stringWidth (const Dstr &s) const;
  const unsigned fontHeight()                const;
  const unsigned oughtHeight()               const;
  const unsigned oughtVerticalMargin()       const;
  const unsigned hourTickLen()               const;
  const unsigned hourTickVerticalMargin()    const;
  const unsigned depthLabelLeftMargin()      const;
  const unsigned depthLabelRightMargin()     const;
  const unsigned depthLineVerticalMargin()   const;
  const int blurbMargin()                    const;
  const double aspectFudgeFactor()           const;
  const unsigned startPosition (unsigned labelWidth) const;
  void drawStringP (int x, int y, const Dstr &s);

  // Overridden virtual setPixel draws asterisks by default.
  void setPixel (int x, int y, Colors::Colorchoice c);

  // New setPixel draws the specified character.
  void setPixel (int x, int y, char c);

  // For line drawing, use -, |, and + instead of asterisks.
  void drawHorizontalLineP (int xlo, int xhi, int y, Colors::Colorchoice c);
  void drawHourTick (double x, Colors::Colorchoice c, bool thick);
  void drawX (double x, double y);

  // Specialization to support use of the five horizontal line-drawing
  // characters from DEC Special Graphics.  (There seems to be no matching
  // set of line-drawing characters in Unicode?)
  void drawHorizontalLinePxSy (int xlo, int xhi, double y,
			       Colors::Colorchoice c);

  // True if VT100 sequences should be used.
  const bool VT100_mode;

  // Postprocess output for VT100.  On input, the high bit of characters is
  // used to indicate characters from the Special Graphics set.  On output,
  // SI and SO have been inserted where needed and all characters have been
  // reduced to 7 bits.
  void VT100_postproc (Dstr &text);
};

}
