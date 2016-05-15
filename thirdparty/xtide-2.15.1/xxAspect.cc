// $Id: xxAspect.cc 2641 2007-09-02 21:31:02Z flaterco $

/*  xxAspect  Get an aspect from the user.  If successful, pass it to
    caller.aspect (double newAspect).

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
#include "xxSimplePrompt.hh"
#include "xxAspect.hh"


void xxAspect::callback (const Dstr &value) {
  double temp;
  Global::GetDoubleReturn gdr = Global::getDouble (
                                                 value,
						 Configurable::posDoubleInterp,
						 temp);
  switch (gdr) {
  case Global::inputNotOK:
  case Global::emptyInput:
    break;
  case Global::inputOK:
    _caller.aspect (temp);
    break;
  default:
    assert (false);
  }
}


xxAspect::xxAspect (const xxWidget &parent,
		    xxPredictionWindow &caller,
		    double initAspect):
  xxSimplePrompt (parent, caller, "Enter Aspect") {

  static constString helpText ("The normal graph aspect is 1.0.  A value of 2.0 will stretch\n\
the graph to be only half as crowded as normal.  A value of 0.5\n\
will compress the graph to be twice as crowded as normal.  You must\n\
enter a value greater than 0.\n\
\n\
Please enter new aspect.");

  char init[80];
  sprintf (init, "%0.2f", initAspect);

  construct (helpText, init);
}

// Cleanup2006 Done
