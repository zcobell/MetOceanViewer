// $Id: Graph.hh 5748 2014-10-11 19:38:53Z flaterco $

/*  Graph  Abstract superclass for all graphs.

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

namespace libxtide {

class Graph {
public:
  enum GraphStyle {normal, clock};
  Graph (unsigned xSize, unsigned ySize, GraphStyle style = normal);
  virtual ~Graph();

  // angle is a kludge to help out the tide clock icon.
  void drawTides (Station *station,
                  Timestamp nominalStartTime,
                  Angle *angle = NULL);

  // Data type that needs to be public for the sake of some hidden
  // functions.
  struct EventBlurb {
    double x;        // Nominal position.
    int deltaLeft;   // x + deltaLeft = lowest used pixel.
    int deltaRight;  // x + deltaRight = highest used pixel.
    Dstr line1;      // Upper line of text.
    Dstr line2;      // Lower line of text.
  };

protected:

  const GraphStyle _style;
  unsigned _xSize, _ySize;

  // Margin left at top and bottom of tide graphs when scaling tides;
  // how much "water" at lowest tide; how much "sky" at highest tide.
  // This is a scaling factor for the graph height.
  static const double vertGraphMargin;

#ifdef blendingTest
  struct BlendBlob {
    double firsty;
    double secondy;
    bool   isNull;
  };
#endif


  // The complex tangle of methods below is to minimize the amount of
  // duplicated code between RGBGraph, SVGGraph, xxPixmapGraph, TTYGraph, and
  // Banner.

  virtual const bool isBanner() const;

  // Determine which depth lines will be drawn.
  // lineStep, minDepth and maxDepth are in tenths of a unit.
  // If no lines at all, minDepth_out=INT_MAX and maxDepth_out=INT_MIN.
  void checkDepth (double ymax,
                   double ymin,
                   double valmax,
                   double valmin,
                   unsigned lineStep,
                   int &minDepth_out,
                   int &maxDepth_out) const;

  // Determine lineStep, labelWidth, labelRight, minDepth, and maxDepth.
  // lineStep:  depth line increment in tenths of a unit.
  // labelWidth:  space allowed for depth labels, including margins.
  // labelRight:  position to which depth labels should be right-aligned.
  // minDepth and maxDepth are as provided by checkDepth.
  void figureLabels (double ymax,
		     double ymin,
		     double valmax,
		     double valmin,
		     const Dstr &unitsDesc,
		     unsigned &lineStep_out,
		     unsigned &labelWidth_out,
		     unsigned &labelRight_out,
		     int &minDepth_out,
		     int &maxDepth_out) const;

  // Draw depth lines.
  // lineStep, minDepth and maxDepth are in tenths of a unit.
  void drawDepth (double ymax,
                  double ymin,
                  double valmax,
                  double valmin,
                  unsigned lineStep,
                  unsigned labelWidth,
                  int minDepth,
                  int maxDepth);

  // This fills in the background, which indicates sunrise/sunset.
  void clearGraph (Timestamp startTime,
                   Timestamp endTime,
                   Interval increment,
                   Station *station,
                   TideEventsOrganizer &organizer);

  // Draw the actual curve representing tide heights or current velocities.
  // val is raw height/velocity; y is that translated to y coordinates.
  // The val and y vectors run from x=-1 at [0] to x=_xSize+1 at [_xSize+2].
  // yzulu is the y-coordinate corresponding to level 0.
  virtual void drawLevels (const SafeVector<double> &val,
			   const SafeVector<double> &y,
			   double yzulu,
			   bool isCurrent
#ifdef blendingTest
			   , const SafeVector<BlendBlob> &blendBlobs
#endif
			   ) = 0;

  // Mark current time.
  virtual void drawX (double x, double y) = 0;

  // Other high-level drawing functions.
  virtual void drawHourTick  (double x, Colors::Colorchoice c, bool thick);
  virtual void labelHourTick (double x, const Dstr &label);
  virtual void drawTitleLine (const Dstr &title);
  virtual void labelEvent    (int topLine, const EventBlurb &blurb);
  void drawBlurbs (int topLine, SafeVector<EventBlurb> &blurbs);

  // Horizontal font metrics.
  virtual const unsigned stringWidth (const Dstr &s) const = 0;

  // Vertical font metrics.
  virtual const unsigned fontHeight() const = 0;
  virtual const unsigned oughtHeight() const = 0;
  virtual const unsigned oughtVerticalMargin() const = 0;

  // Event blurb metrics.  Sets the values of deltaLeft and deltaRight
  // given line1 and line2.  (Blurbs are not intrinsically vertical or
  // horizontal as they are flipped in banner mode.)
  virtual void measureBlurb (EventBlurb &blurb) const;
  virtual const int blurbMargin() const;

  // Lots of other things that are adjustable by subclasses.
  virtual const unsigned depthLabelLeftMargin()    const;
  virtual const unsigned depthLabelRightMargin()   const;
  // Extra vertical space between depth lines and timestamps or hour ticks.
  virtual const unsigned depthLineVerticalMargin() const;
  virtual const unsigned hourTickLen()             const;
  virtual const unsigned hourTickVerticalMargin()  const;
  virtual const double   aspectFudgeFactor()       const;
  virtual const unsigned startPosition (unsigned labelWidth) const;


  // Low-level drawing functions.

  // Suffix convention to eliminate obfuscatory overloads:
  //   P      pixelated coordinate model
  //   S      continuous scalable coordinate model
  //   PxSy   pixelated x axis, continuous scalable y axis

  // All coordinates use northwest gravity.  Integer coordinates range
  // between 0 and size-1; double coordinates range between 0 and size.
  // Thus, the point (x,y) in double coordinates refers to the upper left
  // hand corner of the pixel specified as (x,y) in integer coordinates.

  // int coordinates are used instead of unsigned so that things can
  // be drawn correctly even when they are only partially visible.

  // Filled rectangles.
  // Ordering of x1 and x2, y1 and y2 are irrelevant.
  virtual void drawBoxS (double x1, double x2, double y1, double y2,
			 Colors::Colorchoice c) = 0;

  // Vertical lines.
  // Line thickness is always 1.
  // Ordering of y1 and y2 is irrelevant.
  virtual void drawVerticalLineS (double x, double y1, double y2,
				  Colors::Colorchoice c) = 0;

  // Horizontal lines.
  // Line thickness is always 1.
  // No line will be drawn if xlo > xhi.
  virtual void drawHorizontalLinePxSy (int xlo, int xhi, double y,
				       Colors::Colorchoice c) = 0;

  // Text.
  // Strings should be drawn downwards from the y coordinate provided.
  virtual void centerStringSxPy (double x, int y, const Dstr &s) = 0;
  virtual void rightJustifyStringS (double x, double y, const Dstr &s) = 0;
  // Line of text instead of y coordinate.
  // From the top it's line 0, 1; from the bottom it's -1, -2.
  void centerStringOnLineSx     (double x, int line, const Dstr &s);


  // These are used in Graph and subclasses.
  // "lo" is what you get with opacity 0.
  // "hi" is what you get with opacity 1.
  // "lo" does not need to be less than "hi".
  const double linterp (double lo,
			double hi,
			double opacity) const;
  const unsigned char linterp (unsigned char lo,
			       unsigned char hi,
			       double opacity) const;
  const unsigned short linterp (unsigned short lo,
				unsigned short hi,
				double opacity) const;

private:
  // Prohibited operations not implemented.
  Graph (const Graph &);
  Graph &operator= (const Graph &);
};

}
