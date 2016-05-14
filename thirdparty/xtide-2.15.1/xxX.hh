// $Id: xxX.hh 4508 2012-05-28 18:01:34Z flaterco $

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

namespace xxX {

  // To make efficient use of X11 you need to know that most of these
  // things that look like big objects are in fact integers or
  // pointers.


  // Constants

  // This is used to help calculate the minimum width of graph and
  // clock windows.  The minimum width is the string width of the
  // command button captions plus minWidthFudgeFactor times the number
  // of buttons.  (All of the buttons must fit on one line to avoid
  // catastrophe.)
  extern const unsigned minWidthFudgeFactor;

  // Size of square icon.
  extern const unsigned iconSize;


  // Variables

  extern XtAppContext appContext;
  extern Display     *display;
  extern Screen      *screen;
  extern int          screenNum;
  extern Window       rootWindow;

  // Colormap and visual used by XTide, and the depth thereof.
  extern Colormap colormap;
  extern Visual*  visual;
  extern unsigned colordepth;

  // Preallocated colors indexed by Colorchoice.
  // (A Pixel is an index into a colormap and is actually unsigned long.)
  extern Pixel pixels[Colors::numColors]; // Wants to be [Colors::Colorchoice]

  // Foreground drawing contexts for five of the colors.
  // GCs are pointers to private structs.
  extern GC textGC,
            backgroundGC,
            markGC,
            tidedotGC,
            currentdotGC;
  #ifdef HAVE_XAW3DXFT
  extern XftColor textXftColor;
  #endif

  // Foreground drawing context where color is changed every time.
  extern GC spareGC;

  // Invert drawing context.
  extern GC invertGC;

  // Compatibility layer for plain fonts vs. Xft.
  // Note that fontHeight is defined in Graph.hh; avoid clash.
  #ifdef HAVE_XAW3DXFT
    typedef XftFont FontStruct;
    #define xxFontHeight(fsptr) (fsptr->height)
    #define fontArgName (char*)XtNxftFont
  #else
    typedef XFontStruct FontStruct;
    #define xxFontHeight(fsptr) (fsptr->ascent + fsptr->descent)
    #define fontArgName (char*)XtNfont
  #endif
  extern XtArgVal monoFontArgValue;

  // Fonts.
  extern FontStruct  *monoFontStruct, *defaultFontStruct;
  extern XFontStruct *iconFontStruct; // Never freetype

  // Uh, "atoms."  (Don't blame me, it's X11 terminology.)
  extern Atom killAtom,
              protocolAtom;

  // We try to obtain a TrueColor visual of depth 15 or greater.  If
  // unsuccessful, displaySucks is true and we follow a protocol
  // suitable for PseudoColor displays.
  extern bool displaySucks;

  // The icon is required to "match" the root window, so we need the
  // following stuff just for the icon.
  extern Visual*  iconVisual;
  extern unsigned iconColordepth;

  // Foreground drawing contexts for the icon.
  extern GC iconTextGC,
            iconBackgroundGC;

  // Default icon (not the special xxClock one).
  extern Pixmap iconPixmap;


  // Functions

  // Connect to X11 and initialize most xxX variables.  Switches
  // recognized by X11 are removed from the command line.  Returns
  // widget for xxRoot.  Do this only once.
  const Widget connect (int &argc, char **argv);

  // Initialize the rest of the xxX variables.  Do this only once, after
  // settings are loaded.  Invokes installColors, loads fonts.
  void connectPostSettingsHook (Widget rootWidget);

  // Initialize or update pixels, iconpixels and GCs based on
  // Global::settings.  Installs fonts in the GCs where applicable.
  void installColors ();

  // Get a color from colormap or allocate it if necessary.  If a
  // color cannot be allocated, this function returns the closest
  // match.
  const Pixel getColor (unsigned short red,
			unsigned short green,
			unsigned short blue);

  // Create a blank Pixmap.
  const Pixmap makePixmap     (unsigned width, unsigned height);
  const Pixmap makeIconPixmap (unsigned width, unsigned height);

  // Create a Pixmap from an Xpm (limit 256 colors).
  // XpmCreatePixmapFromData doesn't use const, so neither can I.
  const Pixmap makePixmap (char **data);

  // Create a Pixmap from 24-bit RGB data (requires !displaySucks).
  const Pixmap makePixmap (unsigned width, unsigned height,
			   const SafeVector<unsigned char> &rgb);

  // Return width of string in pixels.
  // XTextExtents doesn't use const, so neither can I.
  const unsigned stringWidth (XFontStruct *fs, const Dstr &s);
  #ifdef HAVE_XAW3DXFT
  const unsigned stringWidth (FontStruct *fs, const Dstr &s);
  #endif

  // Load a client-side font.
  void getClientSideFont (FontStruct *fs, ClientSide::Font &font_out);

  // If applicable, set XtNbeNiceToColormap to False for the specified Widget.
  #ifdef XtNbeNiceToColormap
    #define setRudeness(w) XtVaSetValues (w, (char*)XtNbeNiceToColormap, (XtArgVal)False, NULL);
  #else
    #define setRudeness(w)
  #endif

  // Wrapper for XtCreateManagedWidget that automatically adds setRudeness.
  Widget createXtWidget (constString name, WidgetClass widget_class,
			 Widget parent, ArgList args, Cardinal num_args);

  // If applicable, set XtNbeNiceToColormap to False for the "threeD"
  // child widget.
  void fixBorder (Widget widget);

  // Turn a wimpy Widget into a manly std::auto_ptr<xxWidget>.
  std::auto_ptr<xxWidget> wrap (Widget widget);
}
