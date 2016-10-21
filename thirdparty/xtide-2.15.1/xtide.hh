// $Id: xtide.hh 5749 2014-10-11 19:42:10Z flaterco $

// Global includes for xtide only (not tide or xttpd).

/*  Copyright (C) 2007  David Flater.

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

#include "libxtide/libxtide.hh"
using namespace libxtide;

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/xpm.h>
#include <X11/cursorfont.h>
#ifdef HAVE_XAW3DXFT
// This became necessary with libXaw3dXft 1.6.2
#include <X11/Xft/Xft.h>
#endif

#define  include(define) #define
#define  define(define)  include(define)
#include define(XAWPATH/Box.h)
#include define(XAWPATH/Form.h)
#include define(XAWPATH/Command.h)
#include define(XAWPATH/Repeater.h)
#include define(XAWPATH/Label.h)
#include define(XAWPATH/List.h)
#include define(XAWPATH/Viewport.h)
#include define(XAWPATH/Toggle.h)
#include define(XAWPATH/AsciiText.h)
#include define(XAWPATH/Dialog.h)
#include define(XAWPATH/SimpleMenu.h)
#include define(XAWPATH/SmeBSB.h)
#include define(XAWPATH/MenuButton.h)
#ifdef HAVE_XAW3D
#include define(XAWPATH/ThreeD.h)
#endif
#undef   define
#undef   include

#include "xxWidget.hh"
#include "xxX.hh"
#include "xxRedrawable.hh"
#include "xxMouseWheelViewport.hh"
#include "xxWindow.hh"
#include "xxGlobal.hh"
#include "xxPredictionWindow.hh"
#include "xxClock.hh"
#include "xxRoot.hh"
