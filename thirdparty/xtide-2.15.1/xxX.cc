// $Id: xxX.cc 6199 2016-01-14 23:39:45Z flaterco $

/*  xxX  Globals for interfacing with X11.

    Copyright (C) 2006  David Flater.

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
#include "libxtide/config.hh"
#include "icon_48x48_3color.xpm.hh"

#ifdef HAVE_XAW3DXFT
#define  include(define) #define
#define  define(define)  include(define)
#include define(XAWPATH/Xaw3dXft.h)
#undef   define
#undef   include
#endif

const unsigned xxX::minWidthFudgeFactor (15U);

// We've had good luck with 48; that's what the CDE demanded.  If 48
// stops working, we need to call XGetIconSizes to find out what the
// window manager wants.  Example code can be found in xtide-2.8.3.
const unsigned xxX::iconSize (48U);

Display *xxX::display=NULL;
Screen *xxX::screen=NULL;
int xxX::screenNum;
Window xxX::rootWindow=0;
Colormap xxX::colormap=0;
Visual *xxX::visual=NULL,
       *xxX::iconVisual=NULL;
unsigned xxX::colordepth,
         xxX::iconColordepth;
GC xxX::textGC=NULL,
   xxX::backgroundGC=NULL,
   xxX::markGC=NULL,
   xxX::tidedotGC=NULL,
   xxX::currentdotGC=NULL,
   xxX::spareGC=NULL,
   xxX::invertGC=NULL,
   xxX::iconTextGC=NULL,
   xxX::iconBackgroundGC=NULL;
#ifdef HAVE_XAW3DXFT
XftColor xxX::textXftColor;
#endif
bool xxX::displaySucks;
Pixel xxX::pixels[Colors::numColors];
xxX::FontStruct *xxX::monoFontStruct=NULL,
                *xxX::defaultFontStruct=NULL;
XFontStruct     *xxX::iconFontStruct=NULL;
XtArgVal xxX::monoFontArgValue;
Atom xxX::killAtom,
     xxX::protocolAtom;
XtAppContext xxX::appContext;
Pixmap xxX::iconPixmap;


static Colormap iconColormap=0;
#ifdef HAVE_XAW3DXFT
static Xaw3dXftData *xaw3dxft_data=NULL;
#endif


// These are only valid if !displaySucks.  They do NOT correspond
// directly to the masks in XVisualInfo.  The way that you get from an
// unsigned short to bits for the Pixel is to mask the unsigned short
// by colorMask and then shift left by colorShift (or right if
// colorShift is negative).  Note that the bits per color may NOT be
// the same, contrary to what is implied by the bits_per_RGB field of
// XVisualInfo.  In 16-bit mode, my PC gives 5 bits red, 6 bits green,
// 5 bits blue.
static unsigned short redMask, greenMask, blueMask;
static int redShift, greenShift, blueShift;


// Additional macros and compatibility layer for plain fonts vs. Xft.
// C.f. the global ones in xxX.hh.
#ifdef HAVE_XAW3DXFT
  #define getFont(name) (XftFontOpenName (display, screenNum, name))
  #define glyphLeftBearing(ext) (-ext.x)
  #define glyphRightBearing(ext) (ext.width - ext.x - 1)
  #define glyphAdvance(ext) (ext.xOff)
  #define glyphAscent(ext) (ext.y)
  #define glyphDescent(ext) (ext.height - ext.y)
  #define glyphWidth(ext) (ext.width)
  #define glyphHeight(ext) (ext.height)
  #define createDraw(p,fs,xc) XftDraw *draw = XftDrawCreate (display, p, visual, colormap); \
    XftColor drawColor = {xc.pixel, {xc.red, xc.green, xc.blue, USHRT_MAX}}
  #define drawString(fs,s,l,x,y) (XftDrawString8 (draw, &drawColor, fs, x, y, (FcChar8*)s, l))
  #define freeDraw XftDrawDestroy (draw)
#else
  #define getFont(name) (XLoadQueryFont (display, name))
  #define glyphLeftBearing(ext) (ext.lbearing)
  #define glyphRightBearing(ext) (ext.rbearing - 1)
  #define glyphAdvance(ext) (ext.width)
  #define glyphAscent(ext) (ext.ascent)
  #define glyphDescent(ext) (ext.descent)
  #define glyphWidth(ext) (ext.rbearing - ext.lbearing)
  #define glyphHeight(ext) (ext.ascent + ext.descent)
  #define createDraw(p,fs,xc) XGCValues gcv; \
    gcv.foreground = xc.pixel; \
    gcv.font = fs->fid; \
    GC drawColor = XCreateGC (display, p, GCForeground | GCFont, &gcv)
  #define drawString(fs,s,l,x,y) (XDrawString (display, glyphPixmap, drawColor, x, y, s, l))
  #define freeDraw XFreeGC (display, drawColor)
#endif


// This converts the XVisualInfo masks to what we want.
static void convertMask (unsigned long vinfomask, unsigned short &mask,
int &shift) {
  assert (vinfomask);

  unsigned short highbit = 1 << (sizeof(unsigned short) * CHAR_BIT - 1);
  shift = 0;

  if (vinfomask > USHRT_MAX) {
    // Will need to shift left.
    while (vinfomask > USHRT_MAX) {
      ++shift;
      vinfomask >>= 1;
    }
  } else if (!(vinfomask & highbit)) {
    // Will need to shift right.
    while (!(vinfomask & highbit)) {
      --shift;
      vinfomask <<= 1;
    }
  }

  assert (vinfomask);
  assert (vinfomask <= USHRT_MAX);
  mask = (unsigned short)vinfomask;
}


static void enableTrueColorVisual (const XVisualInfo &vinfo) {
  assert (!xxX::displaySucks);
  // warning: 'vinfo.blah blah may be used uninitialized in this function
  // Wrong.
  #if (__GNUC__ * 100 + __GNUC_MINOR__) >= 407
  quashWarning(-Wmaybe-uninitialized)
  #else
  quashWarning(-Wuninitialized)
  #endif
  assert (vinfo.c_class == TrueColor && vinfo.depth >= 15);
  xxX::visual = vinfo.visual;
  xxX::screenNum = vinfo.screen;
  xxX::colordepth = vinfo.depth;
  convertMask (vinfo.red_mask, redMask, redShift);
  convertMask (vinfo.green_mask, greenMask, greenShift);
  convertMask (vinfo.blue_mask, blueMask, blueShift);
  unquashWarning
  require (xxX::colormap = XCreateColormap (xxX::display,
                                            xxX::rootWindow,
                                            xxX::visual,
                                            AllocNone));
}


static const Pixel remap (unsigned short color,
			  unsigned short mask,
			  int shift) {
  Pixel temp = color & mask;
  // Experiments indicated that shifting by negative values does not work.
  if (shift > 0)
    temp <<= shift;
  else
    temp >>= -shift;
  return temp;
}


static const unsigned short unmap (Pixel pixel,
				   unsigned short mask,
				   int shift) {
  if (shift > 0)
    pixel >>= shift;
  else
    pixel <<= -shift;
  return pixel & mask;
}


static const double cdelta (unsigned short a, unsigned short b) {
  double d = (double)a-(double)b;
  return d*d;
}


static const Pixel getColor (unsigned short red,
			     unsigned short green,
			     unsigned short blue,
			     Visual *gcvisual,
			     Colormap gccolormap,
			     bool sucks) {

  // On a TrueColor display, XAllocColor is redundant.
  if (!sucks) {
    Pixel temp = 0;
    temp |= remap (red, redMask, redShift);
    temp |= remap (green, greenMask, greenShift);
    temp |= remap (blue, blueMask, blueShift);
    return temp;
  }

  // On a sucky display we really need to allocate room in the
  // colormap.  This is usable on a TrueColor visual too, but I used
  // to have trouble with fully saturated color values getting
  // truncated and muted.
  XColor cdef;
  cdef.red = red;
  cdef.green = green;
  cdef.blue = blue;
  cdef.flags = DoRed | DoGreen | DoBlue;
  if (!(XAllocColor (xxX::display, gccolormap, &cdef))) {
    XColor tryit, closest;

    /* closest_distance must be initialized to avoid floating */
    /* point exception on DEC Alpha OSF1 V3.0 */
    double distance, closest_distance = 0.0;

    for (int looper=0; looper<gcvisual->map_entries; ++looper) {
      tryit.pixel = (Pixel) looper;
      tryit.flags = DoRed | DoGreen | DoBlue;
      XQueryColor (xxX::display, gccolormap, &tryit);
      distance = cdelta (tryit.red, cdef.red)
               + cdelta (tryit.green, cdef.green)
               + cdelta (tryit.blue, cdef.blue);
      assert (distance >= 0.0);
      if (distance < closest_distance || (!looper)) {
        closest_distance = distance;
        closest = tryit;
      }
    }
    fprintf (stderr, "XTide:  Can't allocate color; using substitute (badness %lu)\n", (unsigned long)(sqrt (closest_distance)));
    if (!(XAllocColor (xxX::display, gccolormap, &closest)))
      fprintf (stderr, "XTide:  ACK!  Can't allocate that either!  Expect color shifting.\n");
    return (closest.pixel);
  }
  return (cdef.pixel);
}


const Pixel xxX::getColor (unsigned short red,
			   unsigned short green,
			   unsigned short blue) {
  return ::getColor (red, green, blue, visual, colormap, displaySucks);
}


static const Pixel getColor (const Dstr &color,
			     Visual *gcvisual,
			     Colormap gccolormap,
			     bool sucks) {
  XColor cdef;
  if (!(XParseColor (xxX::display, gccolormap, color.aschar(), &cdef))) {
    Dstr details ("The offending color spec was ");
    details += color;
    details += ".";
    Global::barf (Error::BADCOLORSPEC, details);
  }
  return ::getColor (cdef.red, cdef.green, cdef.blue, gcvisual, gccolormap,
    sucks);
}


static const Pixel getColor (const Dstr &color) {
  return ::getColor (color, xxX::visual, xxX::colormap, xxX::displaySucks);
}


static const Pixel getIconColor (const Dstr &color) {
  return ::getColor (color, xxX::iconVisual, iconColormap, true);
}


static const Pixmap makePixmap (unsigned width,
				unsigned height,
				unsigned mpdepth) {
  Pixmap temp;
  require (temp = XCreatePixmap (xxX::display, xxX::rootWindow, width, height,
    mpdepth));
  return temp;
}


void xxX::installColors () {

  assert (display);
  assert (colormap);
  assert (iconColormap);
  assert (rootWindow);
  assert (monoFontStruct);
  assert (iconFontStruct);

  // [0] is icon background.
  // [1] is icon foreground (text).
  static Pixel iconPixels[2];

  static bool first = true;

  if (!first) {
    if (displaySucks)
      XFreeColors (display, colormap, pixels, Colors::numColors, 0);
    XFreeColors (display, iconColormap, iconPixels, 2, 0);
  }

  for (unsigned a=0; a<Colors::numColors; ++a)
    pixels[a] = ::getColor (Global::settings[Colors::colorarg[a]].s);
  iconPixels[0] = ::getIconColor (Global::settings["dc"].s);
  iconPixels[1] = ::getIconColor (Global::settings["fg"].s);

  #ifdef HAVE_XAW3DXFT
  // Unclear why Xft has a different set of color structs and functions.
  // Copying in the pixels already allocated seems to suffice.
  {
    XColor xc = {pixels[Colors::foreground], 0, 0, 0,
	         DoRed | DoGreen | DoBlue, '\0'};
    XQueryColor (display, colormap, &xc);
    textXftColor = {xc.pixel, {xc.red, xc.green, xc.blue, USHRT_MAX}};
  }
  #endif

  if (first) {

    // Do not use XtGetGC.  It returns a "sharable, read-only GC."  We
    // need non-shared, modifiable GCs.

    // Oddly, XCreateGC requires a Drawable.  We don't have any realized
    // windows yet that match the main visual, so we'll have to create a
    // temporary Drawable for it.  For the icon we can use the root
    // window.

    XGCValues gcv;

    {
      // Avoid catch-22 on makePixmap (1, 1), which uses backgroundGC.
      Pixmap tempDrawable = ::makePixmap (1, 1, colordepth);

      gcv.foreground = pixels[Colors::foreground];
      textGC = XCreateGC (display, tempDrawable, GCForeground, &gcv);
      gcv.foreground = pixels[Colors::mark];
      markGC = XCreateGC (display, tempDrawable, GCForeground, &gcv);
      gcv.foreground = pixels[Colors::tidedot];
      tidedotGC = XCreateGC (display, tempDrawable, GCForeground, &gcv);
      gcv.foreground = pixels[Colors::currentdot];
      currentdotGC = XCreateGC (display, tempDrawable, GCForeground, &gcv);
      gcv.foreground = pixels[Colors::background];
      backgroundGC = XCreateGC (display, tempDrawable, GCForeground, &gcv);
      spareGC = XCreateGC (display, tempDrawable, GCForeground, &gcv);
      gcv.function = GXinvert;
      invertGC = XCreateGC (display, tempDrawable, GCFunction, &gcv);

      XFreePixmap (display, tempDrawable);
    }

    gcv.foreground = iconPixels[0];
    iconBackgroundGC = XCreateGC (display, rootWindow, GCForeground, &gcv);
    gcv.foreground = iconPixels[1];
    iconTextGC = XCreateGC (display, rootWindow, GCForeground, &gcv);

#ifndef HAVE_XAW3DXFT
    XSetFont (display, textGC, defaultFontStruct->fid);
#endif
    XSetFont (display, iconTextGC, iconFontStruct->fid);

    first = false;

  } else {
    XSetForeground (display, textGC,            pixels[Colors::foreground]);
    XSetForeground (display, markGC,            pixels[Colors::mark]);
    XSetForeground (display, tidedotGC,         pixels[Colors::tidedot]);
    XSetForeground (display, currentdotGC,      pixels[Colors::currentdot]);
    XSetForeground (display, backgroundGC,      pixels[Colors::background]);
    XSetForeground (display, iconBackgroundGC,  iconPixels[0]);
    XSetForeground (display, iconTextGC,        iconPixels[1]);
  }
}


static const Pixmap makePixmap (char **data,
				Visual *mpvisual,
				Colormap mpcolormap,
				unsigned mpdepth) {
  assert (data);
  Pixmap temp;
  XpmAttributes xpma;
  xpma.valuemask = XpmColormap | XpmDepth | XpmCloseness | XpmExactColors;
  if (mpvisual)
    xpma.valuemask |= XpmVisual;
  xpma.visual = mpvisual;
  xpma.colormap = mpcolormap;
  xpma.depth = mpdepth;
  xpma.closeness = 1000000;  // Anything worth doing is worth overdoing.
  xpma.exactColors = 0;
  int retval = XCreatePixmapFromData (xxX::display, xxX::rootWindow,
    data, &temp, NULL, &xpma);
  switch (retval) {
  case XpmColorError:
    fprintf (stderr, "Warning:  Non-fatal Xpm color error reported.\n");
  case XpmSuccess:
    return temp;
  case XpmOpenFailed:
    Global::barf (Error::XPM_ERROR, "Xpm: Open Failed.");
  case XpmFileInvalid:
    Global::barf (Error::XPM_ERROR, "Xpm: File Invalid.");
  case XpmNoMemory:
    Global::barf (Error::XPM_ERROR, "Xpm: No Memory.");
  case XpmColorFailed:
    return 0;
  }
  if (retval < 0)
    Global::barf (Error::XPM_ERROR, "Fatal Xpm error of unrecognized type reported.");
  fprintf (stderr, "Warning:  Non-fatal Xpm error of unrecognized type reported.\n");
  return temp;
}


// Create a Pixmap from an Xpm (limit 256 colors).
const Pixmap xxX::makePixmap (char **data) {
  return ::makePixmap (data, visual, colormap, colordepth);
}


// Scale 8-bit color values to 16-bit ones.
// The man page clearly states that colors range from 0 to 65535; this
// code will break if that ever changes.
static inline unsigned short scalecolor (unsigned char a) {
  return ((unsigned short)a << 8) | a;
}


// Create a Pixmap from 24-bit RGB data (requires !displaySucks).
// Based on show_image from wxyzv 1.1.1.
const Pixmap xxX::makePixmap (unsigned width, unsigned height,
			      const SafeVector<unsigned char> &rgb) {
  assert (!displaySucks);

  // Guess bits_per_pixel as either 16 or 32.  There seems to be no
  // way to ask directly, and sizeof(Pixel) is not indicative.
  const unsigned bytes_per_pixel = (colordepth > 16 ? 4 : 2);

  // No fancy vectors here; XDestroyImage will deallocate the buffer.
  char *cooked_image;
  require (cooked_image = (char *) malloc (width*height*bytes_per_pixel));

  unsigned idindex=0;
  for (unsigned y=0; y<height; ++y) {
    for (unsigned x=0; x<width; ++x) {
      const unsigned short red   = scalecolor (rgb[idindex++]);
      const unsigned short green = scalecolor (rgb[idindex++]);
      const unsigned short blue  = scalecolor (rgb[idindex++]);
      const Pixel color = getColor (red, green, blue);
      const unsigned pixelindex = y * width + x;
      switch (bytes_per_pixel) {
      case 2:
	((uint16_t *)cooked_image)[pixelindex] = color;
	break;
      case 4:
	((uint32_t *)cooked_image)[pixelindex] = color;
	break;
      default:
	assert (false);
      }
    }
  }

  XImage *ximage = XCreateImage (display, visual, colordepth, ZPixmap, 0,
				 cooked_image, width, height,
				 bytes_per_pixel*8, width*bytes_per_pixel);
  Pixmap pixmap;
  require (pixmap = XCreatePixmap (display, rootWindow, width, height,
				   colordepth));
  XPutImage (display, pixmap, textGC, ximage, 0, 0, 0, 0, width, height);
  // "Note that when the image is created using XCreateImage, XGetImage, or
  // XSubImage, the destroy procedure that the XDestroyImage function calls
  // frees both the image structure and the data pointed to by the image
  // structure."
  XDestroyImage (ximage);
  return pixmap;
}


static const Pixmap makeIconPixmap (char **data) {
  return ::makePixmap (data, xxX::iconVisual, iconColormap,
                       xxX::iconColordepth);
}


const Pixmap xxX::makePixmap (unsigned width, unsigned height) {
  Pixmap temp = ::makePixmap (width, height, colordepth);
  XFillRectangle (display, temp, backgroundGC, 0, 0, width, height);
  return temp;
}


const Pixmap xxX::makeIconPixmap (unsigned width, unsigned height) {
  Pixmap temp = ::makePixmap (width, height, iconColordepth);
  XFillRectangle (display, temp, iconBackgroundGC, 0, 0, width, height);
  return temp;
}


const unsigned xxX::stringWidth (XFontStruct *fs, const Dstr &s) {
  assert (fs);
  assert (!s.isNull());
  return (unsigned)XTextWidth(fs,s.aschar(),s.length());
}


#ifdef HAVE_XAW3DXFT
const unsigned xxX::stringWidth (FontStruct *fs, const Dstr &s) {
  assert (fs);
  assert (!s.isNull());
  XGlyphInfo extents;
  XftTextExtents8 (xxX::display, fs, (FcChar8*)s.aschar(), s.length(), &extents);
  return extents.width;
}
#endif


const Widget xxX::connect (int &argc, char **argv) {
  static bool onlyonce (true);
  assert (onlyonce);
  onlyonce = false;

  // 2008-01  Workaround for an extreme example of code rot (breakage
  // inflicted by backward-incompatible changes occurring in the
  // environment).  The Composite extension throws ARGB visuals at
  // unsuspecting legacy applications.  ARGB visuals are reported as
  // TrueColor visuals, which is a lie.  They aren't substitutable.  The
  // alpha channel defaults to transparent, so an application that doesn't
  // know it's there will NOT work correctly.  There isn't even a proper way
  // to determine if an ARGB visual is what you got!
  static char env_string[] = "XLIB_SKIP_ARGB_VISUALS=1";
  require (putenv (env_string) == 0);

  #ifdef HAVE_XAW3DXFT
    GET_XAW3DXFT_DATA(xaw3dxft_data);
    assert (xaw3dxft_data);
    xaw3dxft_data->encoding = 8;          // Encoding is Latin-1, not UTF-8.
    xaw3dxft_data->menu_spacing = 0;      // Get rid of extra vertical spacing in menus.
    xaw3dxft_data->multi_column_menu = 1; // Make Set Time dialog not suck.
    xaw3dxft_data->no_hilit_reverse = 1;  // Required for Xaw3dXftMultiColumnMenu.

    // Tweaks recommended by J.-P.
    // xaw3dxft_data->button_dashed = 1;  // Make button mouse-over harder to see.
    xaw3dxft_data->proc->set_insensitive_twist((char*)"#a00000");
    xaw3dxft_data->proc->set_hilit_color((char*)"#332211");
  #endif

  Widget widget;

  // See if we can get a nice true color visual (code fragment 11-5, X
  // Toolkit Cookbook).  Somebody can set the default to 8 plane
  // PseudoColor even when 24 bit color is supported, so you have to
  // explicitly ask for a TrueColor visual to get it.

  XtToolkitInitialize();
  appContext = XtCreateApplicationContext();

  // Special handling of -geometry and possibly -font.
  // Concatenation of the argument is not supported here.
  // It's not supported by standard X11 programs either.
  {
    for (int a=0; a<argc; ++a) {
      if (!strncmp (argv[a], "-ge", 3) || !strcmp (argv[a], "-g"))
	strcpy (argv[a], "-X");
      #ifdef HAVE_XAW3DXFT
      // XtOpenDisplay does not take -f or -fo.
      else if (!strncmp (argv[a], "-fon", 4) || !strcmp (argv[a], "-fn"))
	strcpy (argv[a], "-XX");
      #endif
    }
  }

  display = XtOpenDisplay (appContext, NULL, "XTide", "XTide", NULL, 0,
                           &argc, argv);
  if (!display)
    Global::barf (Error::CANTOPENDISPLAY);

  screenNum = DefaultScreen (display);
  screen = ScreenOfDisplay (display, screenNum);
  rootWindow = RootWindow (display, screenNum);
  protocolAtom = XInternAtom (display, "WM_PROTOCOLS", False);
  killAtom = XInternAtom (display, "WM_DELETE_WINDOW", False);
  // Must use default visual and default colormap for icon.
  iconVisual = DefaultVisual (display, screenNum);
  iconColormap = DefaultColormap (display, screenNum);
  iconColordepth =  DefaultDepth (display, screenNum);
  iconPixmap = ::makeIconPixmap (icon_48x48_3color);

  // Find best available TrueColor visual
  XVisualInfo vinfo;
  colordepth = 0;
  {
    XVisualInfo vinfo_template;
    vinfo_template.c_class = TrueColor;
    vinfo_template.screen = screenNum;
    int nitems;
    XVisualInfo *vinfo_out = XGetVisualInfo (display,
      VisualClassMask|VisualScreenMask, &vinfo_template, &nitems);
    for (int l = 0; l < nitems; ++l)
      if ((vinfo_out[l].depth >= 15) &&
	  ((unsigned)vinfo_out[l].depth > colordepth)) {
	colordepth = vinfo_out[l].depth;
	vinfo = vinfo_out[l];
      }
    XFree (vinfo_out);
  }

  displaySucks = (colordepth == 0);
  // Undocumented switch for testing default visual and simple protocol.
  if (argc == 2)
    if (!strcmp (argv[1], "-suck"))
      displaySucks = true;

  if (!displaySucks) {
    enableTrueColorVisual (vinfo);
    Arg args[5] = {
      {XtNdepth, (XtArgVal)colordepth},
      {XtNvisual, (XtArgVal)visual},
      {XtNcolormap, (XtArgVal)colormap},
      {XtNborderColor, (XtArgVal)0},
      {XtNbackground, (XtArgVal)0}
    };
    widget = XtAppCreateShell ("XTide", "XTide",
      applicationShellWidgetClass, display, args, 5);

  } else {
    widget = XtAppCreateShell ("XTide", "XTide",
      applicationShellWidgetClass, display, NULL, 0);
    Arg args[3] = {
      {XtNcolormap, (XtArgVal)(&colormap)},
      {XtNvisual, (XtArgVal)(&visual)},
      {XtNdepth, (XtArgVal)(&colordepth)}
    };
    XtGetValues (widget, args, 3);
  }

  if (!visual)
    visual = DefaultVisual (display, screenNum);
  return widget;
}


#if 0
static void printClientSideFontInitializers (const ClientSide::Font &f) {
  // The good one (C++11 extended initializer lists), with hooks for
  // the mediocre one (compound literals as GNU C++ extension).
  printf (" {%u,%u,{\n", f.height, f.oughtHeight);
  for (SafeVector<ClientSide::Glyph>::const_iterator git (f.glyphs.begin());
       git != f.glyphs.end(); ++git) {
    printf ("  G{%u,{", git->advance);
    for (SafeVector<ClientSide::Pixel>::const_iterator pit (git->pixels.begin());
         pit != git->pixels.end(); ++pit) {
      if (pit != git->pixels.begin()) putchar (',');
      printf ("P{%d,%d,%u}", pit->x, pit->y, pit->opacity);
    }
    printf ("}}");
    if (git+1 != f.glyphs.end()) putchar (',');
    putchar ('\n');
  }
  printf (" }}\n\n");

  // The bad one (runtime construction) for Visual Studio 2012.
  printf ("  Global::graphFont.glyphs.reserve (256);\n");
  printf ("  Global::graphFont.height = %u;\n", f.height);
  printf ("  Global::graphFont.oughtHeight = %u;\n", f.oughtHeight);
  for (SafeVector<ClientSide::Glyph>::const_iterator git (f.glyphs.begin());
       git != f.glyphs.end(); ++git) {
    printf ("  {\n");
    printf ("    ClientSide::Glyph g;\n");
    printf ("    g.advance = %u;\n", git->advance);
    if (!git->pixels.empty()) {
      printf ("    {\n");
      printf ("      const ClientSide::Pixel p[] = {");
      for (SafeVector<ClientSide::Pixel>::const_iterator pit (git->pixels.begin());
           pit != git->pixels.end(); ++pit) {
        if (pit != git->pixels.begin()) putchar (',');
        printf ("{%d,%d,%u}", pit->x, pit->y, pit->opacity);
      }
      printf ("};\n");
      printf ("      g.pixels.assign (p, p+%lu);\n", git->pixels.size());
      printf ("    }\n");
    }
    printf ("    Global::graphFont.glyphs.push_back (g);\n");
    printf ("  }\n");
  }
}
#endif


void xxX::connectPostSettingsHook (Widget rootWidget) {
  static bool onlyonce (true);
  assert (onlyonce);
  onlyonce = false;

  assert (display);
  assert (rootWidget);

  // 2006-06:  Under Gentoo, the schumacher fonts are now optional.
  // Must at least report the name of the missing font.

  // Changing monofont or graphfont settings requires a restart.
  static char *monoFontName = Global::settings["mf"].s.asdupchar();
  constString iconFontName =
    "-schumacher-clean-medium-r-normal-*-10-*-*-*-*-*-*-*";

  // Having a fatal error if monofont or graphfont won't load might prevent
  // the user from unscrewing it in the control panel.
  if (!(monoFontStruct = getFont(monoFontName))) {
    Global::barf (Error::CANT_LOAD_FONT, monoFontName, Error::nonfatal);
    free (monoFontName);
    monoFontName = (char *)defmonofont;
    if (!(monoFontStruct = getFont(monoFontName)))
      Global::barf (Error::CANT_LOAD_FONT, monoFontName, Error::fatal);
  }
  if (!(iconFontStruct = XLoadQueryFont (display, iconFontName)))
    Global::barf (Error::CANT_LOAD_FONT, iconFontName);

  monoFontArgValue = (XtArgVal)
    #ifdef HAVE_XAW3DXFT
      monoFontName;
    #else
      monoFontStruct;
    #endif

  #ifdef HAVE_XAW3DXFT
    // Default font workaround for Xaw3dXft--depends on settings.
    // Normally, when libXaw3dxft is used, the font resource or -fn specified
    // font is processed by XtOpenDisplay and then goes into a black hole,
    // because libXaw3dxft has its own hard-coded default font.

    Configurable &cfbl = Global::settings["XX"];
    assert (xaw3dxft_data);
    if (!cfbl.isNull) {
      constString defaultFontName = cfbl.s.aschar();
      if (!(xaw3dxft_data->default_font = getFont(defaultFontName)))
	Global::barf (Error::CANT_LOAD_FONT, defaultFontName);
      defaultFontStruct = xaw3dxft_data->default_font;
    } else {
      // Coerce DEFAULTFONT out of Xaw3dXft.c.
      defaultFontStruct = xaw3dxft_data->proc->get_font (display, NULL);
    }
  #else
    // The shell has no font attribute.  There is probably a better
    // way to do this.
    Widget labelwidget = xxX::createXtWidget ("", labelWidgetClass,
      rootWidget, NULL, 0);
    Arg fontargs[1] = {
      {XtNfont, (XtArgVal)(&defaultFontStruct)}
    };
    XtGetValues (labelwidget, fontargs, 1);
    XtDestroyWidget (labelwidget);
  #endif
  assert (defaultFontStruct);

  installColors();
  // This uses a GC that is set up in installColors.
  if (Global::settings["gf"].s != "embedded") {
    FontStruct *graphFontStruct;
    constString graphFontName (Global::settings["gf"].s.aschar());
    if (!(graphFontStruct = getFont(graphFontName)))
      Global::barf (Error::CANT_LOAD_FONT, graphFontName, Error::nonfatal);
    else
      getClientSideFont (graphFontStruct, Global::graphFont);
  }
  // printClientSideFontInitializers (Global::graphFont);
}


std::auto_ptr<xxWidget> xxX::wrap (Widget widget) {
  return std::auto_ptr<xxWidget> (new xxWidget (widget));
}


// Create a ClientSide::Font by scraping pixmaps (eww).
// Somebody FIXME to talk directly to libfreetype instead.
// Based on a function by Jean-Pierre Demailly, 2012.
//
// Best results are obtained with a 24-bit TrueColor visual and with sub-pixel
// rendering disabled.  Sub-pixel rendering results in less anti-aliasing and
// slightly thinner glyphs.  16-bit color results in a slight loss of fidelity.
// 8-bit PseudoColor normally results in just more loss of fidelity; however,
// libXft 2.2.0 exhibited background color anomalies and text alignment /
// typesetting anomalies on such visuals.
//
// It is assumed that black pixel will be at zero and the white pixel will be
// at USHRT_MAX, and that gamma will be 1.  If this assumption is violated,
// the correct value of opacity should instead be something like
//   opacity = 255 * pow((g-blackLevel)/(whiteLevel-blackLevel), 1.0/gamma)
//
void xxX::getClientSideFont (FontStruct *fs, ClientSide::Font &font_out) {
  Pixel blackPixel = BlackPixel (display, screenNum),
        whitePixel = WhitePixel (display, screenNum);
  XColor blackX = {blackPixel, 0, 0, 0, DoRed | DoGreen | DoBlue, '\0'},
         whiteX = {whitePixel, 0, 0, 0, DoRed | DoGreen | DoBlue, '\0'};
  XQueryColor (display, colormap, &blackX);
  XQueryColor (display, colormap, &whiteX);
  assert (blackX.green == 0);
  assert (whiteX.green == USHRT_MAX);
  XSetForeground (display, spareGC, blackPixel);
  char ch[2] = {'\0', '\0'};

  // Pass 1.  Find true bounds.
  // Don't waste time on the 64 control characters.
  #ifdef HAVE_XAW3DXFT
    XGlyphInfo extents[256];
    for (unsigned c=32; c<256; (c == 127 ? c=160 : ++c)) {
      ch[0] = (char)c;
      XftTextExtents8 (display, fs, (FcChar8*)ch, 1, &extents[c]);
    }
  #else
    // Having Xft makes this more difficult?
    XCharStruct *extents = fs->per_char;
  #endif
  int minLeftBearing=0, maxRightBearing=0, maxAscent=0, maxDescent=0;
  for (unsigned c=32; c<256; (c == 127 ? c=160 : ++c)) {
    minLeftBearing = std::min (minLeftBearing, (int)glyphLeftBearing(extents[c]));
    maxRightBearing = std::max (maxRightBearing, (int)glyphRightBearing(extents[c]));
    maxAscent = std::max (maxAscent, (int)glyphAscent(extents[c]));
    maxDescent = std::max (maxDescent, (int)glyphDescent(extents[c]));
  }
  const int maxH (maxAscent + maxDescent),
            maxW (maxRightBearing - minLeftBearing + 1);
  assert (maxW > 0 && maxH > 0 && maxAscent > 0);
  font_out.height      = maxAscent;
  font_out.oughtHeight = glyphAscent(extents['0']);
  font_out.glyphs.clear();
  font_out.glyphs.reserve (256);

  // Pass 2.  Scrape the glyphs and make the confusing x and y corrections
  // go away forever.
  Pixmap glyphPixmap = makePixmap (maxW, maxH);
  createDraw (glyphPixmap, fs, whiteX);
  for (unsigned c=0; c<256; ++c) {
    ClientSide::Glyph glyph;
    if ((c & 0x7F) < 32)
      glyph.advance = 0;
    else {
      const int extW = glyphWidth(extents[c]),
		extH = glyphHeight(extents[c]);
      glyph.advance = glyphAdvance(extents[c]);
      if (extW > 0 && extH > 0) {
	const int left = glyphLeftBearing(extents[c]) - minLeftBearing,
		 right = left + extW - 1,
		   top = maxAscent - glyphAscent(extents[c]),
		bottom = top + extH - 1;
	assert (left >= 0 && left <= right && right < maxW);
	assert (top >= 0 && top <= bottom && bottom < maxH);
	XFillRectangle (display, glyphPixmap, spareGC, left, top, extW, extH);
	ch[0] = (char)c;
	drawString (fs, ch, 1, -minLeftBearing, maxAscent);
	XImage *img = XGetImage (display, glyphPixmap, left, top, extW, extH,
				 AllPlanes, ZPixmap);
	for (int y=0; y<extH; ++y) {
	  for (int x=0; x<extW; ++x) {
	    Pixel pixel = XGetPixel (img, x, y);
	    uint8_t green;
	    if (displaySucks) {
	      XColor pixelX = {pixel, 0, 0, 0, DoGreen, '\0'};
	      XQueryColor (display, colormap, &pixelX);
	      green = pixelX.green >> 8;
	    } else {
	      green = 255 * unmap(pixel,greenMask,greenShift) / greenMask;
	    }
	    if (green) {
	      // gcc 4.5.3 failed on this
	      // glyph.pixels.push_back ({x+glyphLeftBearing(extents[c]), y+top, green});
	      ClientSide::Pixel temp = {x+glyphLeftBearing(extents[c]), y+top, green};
	      glyph.pixels.push_back (temp);
	    }
	  }
	}
	XDestroyImage (img);
      }
    }
    // Control characters arrive here as null glyphs.
    font_out.glyphs.push_back (glyph);
  }
  freeDraw;
  XFreePixmap (display, glyphPixmap);
}


Widget xxX::createXtWidget (constString name,
			    WidgetClass widget_class,
			    Widget parent,
			    ArgList args,
			    Cardinal num_args) {
  Widget ret = XtCreateManagedWidget (name, widget_class, parent, args, num_args);
  setRudeness (ret);
  return ret;
}


#if 0
void xxX::listChildren (Widget widget, unsigned indent) {
  String      name = NULL, label = NULL;
  WidgetList  children = NULL;
  Cardinal    numChildren = 0;
  XtVaGetValues (widget, XtNname, (XtArgVal)(&name),
                 XtNlabel, (XtArgVal)(&label),
		 XtNchildren, (XtArgVal)(&children),
		 XtNnumChildren , (XtArgVal)(&numChildren), NULL);
  for (unsigned i=0; i<indent; ++i)
    putchar (' ');
  printf ("\"%s\" \"%s\"\n", name, label);
  indent += 2;
  for (unsigned i=0; i<numChildren; ++i)
    listChildren (children[i], indent);
}
#endif


#ifdef HAVE_XAW3D
void xxX::fixBorder (Widget widget) {
  Widget borderWidget;
  require (borderWidget = XtNameToWidget (widget, "threeD"));
  setRudeness (borderWidget);
}
#else
void xxX::fixBorder (Widget widget unusedParameter) {}
#endif
