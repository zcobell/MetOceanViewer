// $Id: ClientSideFont.hh 5748 2014-10-11 19:38:53Z flaterco $

/*
    Copyright (C) 2012  David Flater.

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

namespace ClientSide {

  struct Pixel {
    int x;           // Can be negative (some glyphs start left of origin).
    int y;
    uint8_t opacity; // Divide by 255 to get 0..1.
  };

  struct Glyph {
    unsigned advance;
    SafeVector<Pixel> pixels;
  };

  struct Font {
    unsigned height;           // For line spacing.
    unsigned oughtHeight;      // For vertical alignment of depth labels.
    SafeVector<Glyph> glyphs;  // size = 256
  };

}

const unsigned stringWidth (const ClientSide::Font &font, const Dstr &s);

}
