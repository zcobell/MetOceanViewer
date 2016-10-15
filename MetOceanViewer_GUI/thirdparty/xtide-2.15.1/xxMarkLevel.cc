// $Id: xxMarkLevel.cc 2641 2007-09-02 21:31:02Z flaterco $

/*  xxMarkLevel  Get a PredictionValue from the user.  If successful, invoke
    caller.mark (newMarkLevel).

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
#include "xxMarkLevel.hh"


void xxMarkLevel::callback (const Dstr &value) {
  double temp;
  Global::GetDoubleReturn gdr = Global::getDouble (value,
						   Configurable::numberInterp,
						   temp);
  switch (gdr) {
  case Global::inputNotOK:
    break;
  case Global::emptyInput:
    _caller.mark (NullablePredictionValue());
    break;
  case Global::inputOK:
    _caller.mark (NullablePredictionValue(units, temp));
    break;
  default:
    assert (false);
  }
}


xxMarkLevel::xxMarkLevel (const xxWidget &parent,
			  xxPredictionWindow &caller,
			  PredictionValue initMarkLevel):
  xxSimplePrompt (parent, caller, "Enter Mark Level"),
  units(initMarkLevel.Units()) {

  Dstr helpText ("Enter mark level in ");
  helpText += Units::longName (units);
  helpText += '.';

  char init[80];
  sprintf (init, "%0.2f", initMarkLevel.val());

  construct (helpText, init);
}

// Cleanup2006 Done
