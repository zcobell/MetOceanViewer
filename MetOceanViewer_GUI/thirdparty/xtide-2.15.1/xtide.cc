// $Id: xtide.cc 5749 2014-10-11 19:42:10Z flaterco $

/*  XTide  Harmonic tide clock and tide predictor.

    Copyright (C) 1997  David Flater.

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


int main (int argc, char **argv) {
  srand (time (NULL));
  // This should never be used (Visual Studio 2012 can't build xtide).
  #ifdef NO_INITIALIZER_LISTS
  Global::initializeGraphFont();
  #endif
  Global::initTerm();
  xxroot = new xxRoot (argc, argv);
  xxroot->run();
  exit (0);
}
