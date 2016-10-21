// $Id: xxTimestampDialog.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxTimestampDialog  Embeddable timestamp chooser.

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
#include "xxMultiChoice.hh"
#include "xxTimestampDialog.hh"


void xxTimestampDialog::val (Dstr &ISO_string_out) {
  char buf[80];
  sprintf (buf, "%04u-%02u-%02u %02u:%02u",
	   yearChoice->choice() + Global::dialogFirstYear,
	   monthChoice->choice() + 1,
	   dayChoice->choice() + 1,
	   hourChoice->choice(),
	   minuteChoice->choice());
  ISO_string_out = buf;
}


xxTimestampDialog::xxTimestampDialog (const xxWidget &parent,
				      constString caption,
				      Timestamp initTime,
				      const Dstr &timezone):
  _timezone(timezone) {

  {
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNorientation, (XtArgVal)XtorientHorizontal}
    };
    Widget boxWidget = xxX::createXtWidget ("", boxWidgetClass,
      parent.widget(), args, 3);
    box = xxX::wrap (boxWidget);
  }
  Arg labelArgs[3] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
    {XtNborderWidth, (XtArgVal)0}
  };
  {
    Dstr padded_caption (caption);
    padded_caption += "  ";
    Widget labelWidget = xxX::createXtWidget (padded_caption.aschar(),
      labelWidgetClass, box->widget(), labelArgs, 3);
    label = xxX::wrap (labelWidget);
  }

  tm initTmStruct (initTime.tmStruct (_timezone));

  // String array for yearChoices is allocated on first use, kept as a
  // static variable and never destroyed.  No cause for alarm.
  static SafeVector<constCharPointer> yearChoices
                        (Global::dialogLastYear - Global::dialogFirstYear + 2);
  static bool firstTime (true);
  if (firstTime) {
    firstTime = false;
    for (unsigned looper (Global::dialogFirstYear);
         looper <= Global::dialogLastYear;
         ++looper) {
      char buf[5];
      sprintf (buf, "%04u", looper);
      yearChoices[looper-Global::dialogFirstYear] = strdup (buf);
    }
    yearChoices[Global::dialogLastYear-Global::dialogFirstYear+1] = NULL;
  }

  static constString monthChoices[] = {"01", "02", "03", "04", "05", "06",
				       "07", "08", "09", "10", "11", "12",
				       NULL};
  static constString dayChoices[] = {
    "01", "02", "03", "04", "05", "06", "07", "08", "09", "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
    "21", "22", "23", "24", "25", "26", "27", "28", "29", "30",
    "31", NULL};
  static constString hourChoices[] = {"00",
    "01", "02", "03", "04", "05", "06", "07", "08", "09", "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
    "21", "22", "23", NULL};
  static constString minuteChoices[] = {"00",
    "01", "02", "03", "04", "05", "06", "07", "08", "09", "10",
    "11", "12", "13", "14", "15", "16", "17", "18", "19", "20",
    "21", "22", "23", "24", "25", "26", "27", "28", "29", "30",
    "31", "32", "33", "34", "35", "36", "37", "38", "39", "40",
    "41", "42", "43", "44", "45", "46", "47", "48", "49", "50",
    "51", "52", "53", "54", "55", "56", "57", "58", "59", NULL};

  unsigned year   = initTmStruct.tm_year + 1900;
  unsigned month  = initTmStruct.tm_mon + 1;
  unsigned day    = initTmStruct.tm_mday;
  unsigned hour   = initTmStruct.tm_hour;
  unsigned minute = initTmStruct.tm_min;

  if (year < Global::dialogFirstYear)
    year = Global::dialogFirstYear;
  else if (year > Global::dialogLastYear)
    year = Global::dialogLastYear;

  yearChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (
						*box,
                                                &(yearChoices[0]),
					      	year-Global::dialogFirstYear));
  monthChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (*box,
								 monthChoices,
								 month-1));
  dayChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (*box,
							       dayChoices,
							       day-1));
  {
    Widget labelWidget = xxX::createXtWidget (" ",
      labelWidgetClass, box->widget(), labelArgs, 3);
    spaceLabel1 = xxX::wrap (labelWidget);
  }
  hourChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (*box,
								hourChoices,
								hour));
  {
    Widget labelWidget = xxX::createXtWidget (":",
      labelWidgetClass, box->widget(), labelArgs, 3);
    spaceLabel2 = xxX::wrap (labelWidget);
  }
  minuteChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (
                                                                *box,
								minuteChoices,
								minute));
}

// Cleanup2006 Done
