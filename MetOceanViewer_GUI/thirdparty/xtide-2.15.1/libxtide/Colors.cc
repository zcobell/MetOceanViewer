// $Id: Colors.cc 5748 2014-10-11 19:38:53Z flaterco $

/*  Colors  Manage XTide colors without X.

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
#include "rgb.hh"

namespace libxtide {


constString Colors::colorarg[numColors] = {
  "bg", "fg", "mc", "bc", "dc", "nc", "fc", "ec", "Dc", "Mc", "tc", "cc"
};


#ifndef SCNx8
#ifdef USE_PRAGMA_MESSAGE
#pragma message("WARNING:  SCNx8 is not defined.  Using workaround.")
#else
#warning SCNx8 is not defined.  Using workaround.
#endif

// 2011-11-27  Visual C++ 2010 has no scan format that works with uint8_t.
// "hhx" and "x" both write zeros into subsequent bytes, corrupting memory.

#define SCNx8 "x"

static int scanRedirect (const char *str,
			 const char *format,
			 uint8_t *x_out) {
  unsigned x;
  int rtn = sscanf (str, format, &x);
  *x_out = x;
  return rtn;
}

static int scanRedirect (const char *str,
			 const char *format,
			 uint8_t *r_out,
			 uint8_t *g_out,
			 uint8_t *b_out) {
  unsigned r, g, b;
  int rtn = sscanf (str, format, &r, &g, &b);
  *r_out = r;
  *g_out = g;
  *b_out = b;
  return rtn;
}

#define sscanf scanRedirect

#endif


const bool Colors::parseColor (const Dstr &colorName,
			       uint8_t &r,
			       uint8_t &g,
			       uint8_t &b,
			       Error::ErrType fatality) {

  r = g = b = 0;
  constString fmt1 = "rgb:%" SCNx8 "/%" SCNx8 "/%" SCNx8;

  // When SCNx8 *is* defined, we can get a lot of:
  // warning: ISO C++ does not support the 'hh' gnu_scanf length modifier
  quashWarning(-Wformat)

  if (sscanf (colorName.aschar(), fmt1, &r, &g, &b) != 3) {

    r = g = b = 0;
    constString fmt2 = "%" SCNx8;

    // Kludge for default fg and bg colors under the CDE
    if (colorName[0] == '#' && colorName.length() == 13) {
      char temp[3];
      temp[2] = '\0';
      temp[0] = colorName[1];
      temp[1] = colorName[2];
      sscanf (temp, fmt2, &r);
      temp[0] = colorName[5];
      temp[1] = colorName[6];
      sscanf (temp, fmt2, &g);
      temp[0] = colorName[9];
      temp[1] = colorName[10];
      sscanf (temp, fmt2, &b);

    // Kludge for default fg and bg colors under Debian
    } else if (colorName[0] == '#' && colorName.length() == 7) {
      char temp[3];
      temp[2] = '\0';
      temp[0] = colorName[1];
      temp[1] = colorName[2];
      sscanf (temp, fmt2, &r);
      temp[0] = colorName[3];
      temp[1] = colorName[4];
      sscanf (temp, fmt2, &g);
      temp[0] = colorName[5];
      temp[1] = colorName[6];
      sscanf (temp, fmt2, &b);

  unquashWarning
    } else {
      for (unsigned rr=0; rgbtxt[rr].name; ++rr)
	if (dstrcasecmp (colorName, rgbtxt[rr].name) == 0) {
	  r = rgbtxt[rr].r;
	  g = rgbtxt[rr].g;
	  b = rgbtxt[rr].b;
	  return true;
	}
      Dstr details ("The offending color spec was ");
      details += colorName;
      details += '.';
      Global::barf (Error::BADCOLORSPEC, details, fatality);
      return false;
    }
  }

  return true;
}

}
