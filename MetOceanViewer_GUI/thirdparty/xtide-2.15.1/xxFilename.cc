// $Id: xxFilename.cc 4497 2012-05-27 00:33:09Z flaterco $

/*  xxFilename  Get a file name and format from the user.  If successful, do
    caller.save (filename, format).

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
#include "xxHorizDialog.hh"
#include "xxMultiChoice.hh"
#include "xxReconfigurable.hh"
#include "xxFilename.hh"


static void xxFilenameCallback (Widget w unusedParameter,
				XtPointer client_data,
				XtPointer call_data unusedParameter) {
  assert (client_data);
  xxFilename *prompts ((xxFilename *)client_data);
  prompts->callback();
  delete prompts->dismiss();
}



static void xxFilenameCancelCallback (Widget w unusedParameter,
				      XtPointer client_data,
				      XtPointer call_data unusedParameter) {
  assert (client_data);
  delete ((xxFilename *)client_data)->dismiss();
}


void xxFilename::callback() {
  Dstr filename (filenameDiag->val());
  if (filename.strchr ('\n') != -1 ||
      filename.strchr ('\r') != -1 ||
      filename.strchr (' ') != -1 ||
      filename[0] == '-') {
    Dstr details ("Well, it's not that I can't do it, it's that you probably\n\
don't want me to.  The filename that you entered is '");
    details += filename;
    details += "'.\n";
    if (filename[0] == '-')
      details += "Filenames that begin with a dash are considered harmful.";
    else
      details += "Whitespace in filenames is considered harmful.";
    Global::barf (Error::CANT_OPEN_FILE, details, Error::nonfatal);
  } else {
    Format::Format form (Format::text);
    if (!_textOnly)
      switch (formatChoice->choice()) {
      case 0:
        form = Format::PNG;
        break;
      case 1:
        form = Format::SVG;
        break;
      default:
        assert (false);
      }
    _caller.save (filename, form);
  }
}


void xxFilename::reconfigure() {
  if (!_textOnly) {
    Dstr filename (filenameDiag->val());
    if (filename.length() > 3) {
      unsigned i (filename.length()-4);
      Dstr extension (filename.ascharfrom(i));
      switch (formatChoice->choice()) {
      case 0:
        if (extension.contains (".svg")) {
          filename -= i;
          filename += ".png";
	  filenameDiag->val (filename);
        }
	break;
      case 1:
        if (extension.contains (".png")) {
          filename -= i;
          filename += ".svg";
  	  filenameDiag->val (filename);
        }
	break;
      default:
	assert (false);
      }
    }
  }
}


xxFilename::~xxFilename() {
  unrealize();
  _caller.noClose = false;
}


xxFilename::xxFilename (const xxWidget &parent,
			xxPredictionWindow &caller,
			bool textOnly):
  xxWindow (parent, boxContainer, XtGrabExclusive),
  _caller(caller),
  _textOnly(textOnly) {

  _caller.noClose = true;
  setTitle ("Save prompts");

  Arg buttonArgs[4] =  {
    {XtNvisual, (XtArgVal)xxX::visual},
    {XtNcolormap, (XtArgVal)xxX::colormap},
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::button]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };

  static constString textChoices[] = {"txt", NULL};
  static constString graphChoices[] = {"png", "svg", NULL};
  formatChoice = std::auto_ptr<xxMultiChoice> (new xxMultiChoice (*container,
								  "Format:",
				       (textOnly ? textChoices : graphChoices),
								  0,
								  *this));

  constString initFilename = (textOnly ? "tides.txt" : "tides.png");
  filenameDiag = std::auto_ptr<xxHorizDialog> (new xxHorizDialog (
                                                                *container,
							        "Filename:",
							        initFilename));
  {
    Widget buttonWidget = xxX::createXtWidget ("Go", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxFilenameCallback,
      (XtPointer)this);
    goButton = xxX::wrap (buttonWidget);
  }{
    Widget buttonWidget = xxX::createXtWidget ("Cancel", commandWidgetClass,
      container->widget(), buttonArgs, 4);
    XtAddCallback (buttonWidget, XtNcallback, xxFilenameCancelCallback,
      (XtPointer)this);
    cancelButton = xxX::wrap (buttonWidget);
  }

  realize();
}

// Cleanup2006 Done
