// $Id: xxHorizDialog.cc 4506 2012-05-27 20:53:01Z flaterco $

/*  xxHorizDialog  More compact replacement for dialogWidgetClass.

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


constString xxHorizDialog::val() const {
  return buf;
}


void xxHorizDialog::val (const Dstr &newVal) {
  assert (newVal.length() < bufSize);
  strcpy (buf, newVal.aschar());
  {
    Arg args[1] =  {
      {XtNstring, (XtArgVal)buf}
    };
    assert (text.get());
    XtSetValues (text->widget(), args, 1);
  }
}


xxHorizDialog::xxHorizDialog (const xxWidget &parent,
			      constString caption,
			      constString init) {
  {
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNorientation, (XtArgVal)XtorientHorizontal}
    };
    Widget boxWidget = xxX::createXtWidget ("", boxWidgetClass,
      parent.widget(), args, 3);
    box = xxX::wrap (boxWidget);
  }{
    Arg args[3] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNborderWidth, (XtArgVal)0}
    };
    Widget labelWidget = xxX::createXtWidget (caption,
      labelWidgetClass, box->widget(), args, 3);
    label = xxX::wrap (labelWidget);
  }
  assert (strlen (init) < bufSize);
  strcpy (buf, init);
  {
    Arg args[6] =  {
      {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
      {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]},
      {XtNstring, (XtArgVal)buf},
      {(char*)XtNuseStringInPlace, (XtArgVal)1},
      {XtNlength, (XtArgVal)(bufSize-1)},
      {XtNeditType, (XtArgVal)XawtextEdit}
    };
    Widget textWidget = xxX::createXtWidget ("", asciiTextWidgetClass,
      box->widget(), args, 6);
    text = xxX::wrap (textWidget);
    // FIXME: Can't access the threeD widget to fix border!
    // It works to patch Text.c in libXaw3dXft-1.6.2 to invoke
    //   XtVaSetValues (ctx->text.threeD, ...
    // but as of here, threeD is NULL.  xxX::fixBorder finds nothing and
    // dies with assertion failure.  Same behavior with libXaw3d 1.5E.
  }
}


void xxHorizDialog::globalRedraw() {
  Arg args[2] =  {
    {XtNbackground, (XtArgVal)xxX::pixels[Colors::background]},
    {XtNforeground, (XtArgVal)xxX::pixels[Colors::foreground]}
  };
  assert (label.get());
  XtSetValues (label->widget(), args, 2);
  assert (text.get());
  XtSetValues (text->widget(), args, 2);
  assert (box.get());
  XtSetValues (box->widget(), args, 2);
}
