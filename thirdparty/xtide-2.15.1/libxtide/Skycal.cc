// $Id: Skycal.cc 5748 2014-10-11 19:38:53Z flaterco $

// Skycal.cc -- Functions for sun and moon events.

// Prediction of moon phases, sun and moon rises and sets has nothing
// to do with tide prediction.  There is no overlap between this code
// and the tide prediction code.

// This source file began its life as skycalendar.c and skycalc.c in
// John Thorstensen's skycal distribution (version 4.1, 1994-09) at
// ftp://iraf.noao.edu/contrib/skycal.tar.Z.  Those portions that are
// unchanged from the original sources are covered by the original
// license statement, included below.  The new portions and "value
// added" by David Flater are covered under the GNU General Public
// License.

// 2013-05-26
//
// Patch from James Ashton to replace etcorr.

// 2003-02-04
//
// The release notes for Skycal V5 indicated that some code in 4.1 had
// copyright problems.  Reviewed all code snippets here and found that
// none were impacted by copyright-related changes.
//
// Harmonized atan_circ with slightly improved V5 version.

/*
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

// The relevant portions of the Skycal 5 comments and license statement
// are as follows:

/* SKY CALCULATOR PROGRAM 
   John Thorstensen, Dartmouth College.  
   This program computes many quantities frequently needed by the 
   observational astronomer.  It is written as a completely 
   self-contained program in standard c, so it should be
   very transportable; the only issue I know of that really affects
   portability is the adequacy of the double-precision floating
   point accuracy on the machine.  Experience shows that c compilers
   on various systems have idiosyncracies, though, so be sure
   to check carefully.

   This is intended as an observatory utility program; I assume the
   user is familiar with astronomical coordinates and nomenclature.
   While the code should be very transportable, I also 
   assume it will be installed by a conscientious person who
   will run critical tests before it is released at a new site.
   Experience shows that some c compilers generate unforseen errors
   when the code is ported, so the output should be checked meticulously
   against data from other sites.

[...]

   The program is self-contained.  It has been developed primarily on 
   UNIX and Linux machines, and should adapt easily to any system with 
   a c compiler.  
   
	** BUT CAUTION ... **
   Because many of the routines take a double-precision floating point
   Julian Date as their time argument, one must be sure that the machine
   and compiler carry sufficient mantissa to reach the desired accuracy.
   On most architectures the double-precision floating point julian date
   has an accuracy of order 0.01 seconds of time, which is just adequate.

LEGALITIES: 

   I make no guarantee as to the accuracy, reliability, or
   appropriateness of this program, though I have found it to be 
   reasonably accurate and quite useful to the working astronomer.
   
   The program is COPYRIGHT 2000 BY JOHN THORSTENSEN.  
   Permission is hereby granted for non-profit scientific or educational use.
   For-profit use (e. g., by astrologers!) must be through negotiated
   license.  The author requests that observatories and astronomy 
   departments which install this as a utility notify the author
   by paper mail, just so I know how widely it is used.     

   Credits:  
    * The julian date and sidereal time routines were 
    originally coded in PL/I by  Steve Maker of Dartmouth College.  
    They were based on routines in the old American Ephemeris.
    Many of the routines were coded from Jean Meeus' "Astronomical
    Formulae for Calculators", published by Willman-Bell.  This is
    an extraordinarily helpful little book!
*/

// The Skycal 4.1 comments and license statement are as follows:

/*
   This is a self-contained c-language program to print a nighttime
   astronomical calendar for use in planning observations.
   It prints to standard output (usually the terminal); the
   operator should capture this output (e. g., using redirection
   in UNIX or the /out= switch in VMS) and then print it on an
   appropriate output device.  The table which is printed is some
   125 columns wide, so a wide device is required (either a line
   printer or a laserprinter in LANDSCAPE mode.)  It is assumed that
   the ASCII form-feed character will actually begin a new page.
   The original program was to run on VMS, but it should be very
   transportable.  Non-vms users will probably want to change
   'unixio.h' to 'stdio.h' in the first line.
   An explanatory text is printed at the beginning of the output, which
   includes the appropriate CAUTIONS regarding accuracy and applicability.

   A number of 'canned site' parameters have been included.  Be
   careful of time zones, DST etc. for foreign sites.
   To customize to your own site, install an option in the
   routine 'load_site'.  The code is very straightforward; just do
   it exactly the same as the others.  You might also want to erase
   some seldom-used choices.  One can also specify new site parameters
   at run time.

   This program may be used freely by anyone for scientific or educational
   purposes.  If you use it for profit, I want a cut, and claim
   a copyright herewith.  In any case please acknowledge the source:

			John Thorstensen
			Dept. of Physics and Astronomy
			Dartmouth College
			Hanover, NH 03755
			John.Thorstensen@dartmouth.edu

			May 26, 1993.
*/

#include "libxtide.hh"
#include "Skycal.hh"

// To avoid creating a clash on every reference of a math.h trig function,
// namespace libxtide is applied only to the non-static definitions.


// DWF:  This affects the rise/set predictions.  Normally you would
// need to adjust it for the elevation of the location, but since this
// is a tide prediction program, we can safely assume that we are
// always at sea level :-)
static const double riseAltitude (-0.83);

#define DEG_IN_RADIAN     57.2957795130823
#define HRS_IN_RADIAN     3.819718634
#define SEC_IN_DAY        86400.
#define GREG_DAYS_IN_YEAR 365.2425     /* 365 + 97 / 400 */
#define FLATTEN           0.003352813  /* flattening of earth, 1/298.257 */
#define EQUAT_RAD         6378137.     /* equatorial radius of earth, meters */
#define J2000             2451545.     /* Julian date at standard epoch */


// Harmonized with Skycal V5 2003-02-04
static double atan_circ(double x, double y)
{
        /* returns radian angle 0 to 2pi for coords x, y --
           get that quadrant right !! */

        double theta;

        if((x == 0.) && (y == 0.)) return(0.);  /* guard ... */

        theta = atan2(y,x);  /* turns out there is such a thing in math.h */
        while(theta < 0.) theta += 2.0 * M_PI;
        return(theta);
}


// This is stripped down from Skycal V4.1 and even more stripped down
// from Skycal V5.
static double altit (double dec, double ha, double lat)
/* dec deg, dec hrs, dec deg */
{
  double x;
  dec = dec / DEG_IN_RADIAN;
  ha = ha / HRS_IN_RADIAN;
  lat = lat / DEG_IN_RADIAN;  /* thank heavens for pass-by-value */
  x = DEG_IN_RADIAN * asin(cos(dec)*cos(ha)*cos(lat) + sin(dec)*sin(lat));
  return(x);
}


// No important changes in Skycal V5.
static double lst(double jd, double longit)

{
	/* returns the local MEAN sidereal time (dec hrs) at julian date jd
	   at west longitude long (decimal hours).  Follows
           definitions in 1992 Astronomical Almanac, pp. B7 and L2.
           Expression for GMST at 0h ut referenced to Aoki et al, A&A 105,
	   p.359, 1982. */

	double t, ut, jdmid, jdint, jdfrac, sid_g;
	long jdin, sid_int;

	jdin = (long)jd;         /* fossil code from earlier package which
			split jd into integer and fractional parts ... */
	jdint = jdin;
	jdfrac = jd - jdint;
	if(jdfrac < 0.5) {
		jdmid = jdint - 0.5;
		ut = jdfrac + 0.5;
	}
	else {
		jdmid = jdint + 0.5;
		ut = jdfrac - 0.5;
	}
	t = (jdmid - J2000)/36525;
	sid_g = (24110.54841+8640184.812866*t+0.093104*t*t-6.2e-6*t*t*t)/86400.;
	sid_int = (long)sid_g;
	sid_g = sid_g - (double) sid_int;
	sid_g = sid_g + 1.0027379093 * ut - longit/24.;
	sid_int = (long)sid_g;
	sid_g = (sid_g - (double) sid_int) * 24.;
	if(sid_g < 0.) sid_g = sid_g + 24.;
	return(sid_g);
}


// No important changes in Skycal V5.
static void
lpsun(double jd, double *ra, double *dec)

/* Low precision formulae for the sun, from Almanac p. C24 (1990) */
/* ra and dec are returned as decimal hours and decimal degrees. */

{
	double n, L, g, lambda,epsilon,x,y,z;

	n = jd - J2000;
	L = 280.460 + 0.9856474 * n;
	g = (357.528 + 0.9856003 * n)/DEG_IN_RADIAN;
	lambda = (L + 1.915 * sin(g) + 0.020 * sin(2. * g))/DEG_IN_RADIAN;
	epsilon = (23.439 - 0.0000004 * n)/DEG_IN_RADIAN;

	x = cos(lambda);
	y = cos(epsilon) * sin(lambda);
	z = sin(epsilon)*sin(lambda);

	*ra = (atan_circ(x,y))*HRS_IN_RADIAN;
	*dec = (asin(z))*DEG_IN_RADIAN;
}


// Fwd Decl
static void accumoon (double jd, double geolat, double lst, double elevsea,
double *topora, double *topodec, double *topodist);


// Not in Skycal.
// This function combines a few steps that are always used together to
// find the altitude of the sun or moon.
static double altitude (double jd, double lat, double longit, bool lunar) {
  if (lunar) {
    double ra, dec, dist, sid;
    sid = lst(jd,longit);
    accumoon (jd, lat, sid, 0, &ra, &dec, &dist);
    return altit(dec, sid-ra, lat);
  } else {
    double ra, dec;
    lpsun(jd, &ra, &dec);
    return altit(dec, lst(jd,longit)-ra, lat);
  }
}


// I messed with this a good bit.
//
//   *  It now converges to within 1 minute.
//   *  It converges better from bad initial guesses.  (Deriv is now
//      updated inside of the loop.)
//   *  It won't roam more than half a day in either direction.
//   *  Max iterations chosen conservatively.
//   *  It finishes with a check to determine what it found.

// 2003-02-04
// Expanded to handle moonrise/moonset (replacing jd_moon_alt too).

// No important changes in Skycal V5.
static double jd_alt (double alt, double jdorig, double lat, double longit,
bool lunar, bool &is_rise)
{
	/* returns jd at which sun/moon is at a given
		altitude, given jdguess as a starting point. */

        double jdguess = jdorig;
	double jdout, adj = 1.0;
	double deriv, err, del = 0.002;
	double alt2,alt3;
	short i = 0;

	/* first guess */

	alt2 = altitude (jdguess, lat, longit, lunar);
	jdguess = jdguess + del;
	alt3 = altitude (jdguess, lat, longit, lunar);
	err = alt3 - alt;
	deriv = (alt3 - alt2) / del;
        if (deriv == 0.0)
          return (-1.0e10); // Found dead end.
        adj = -err/deriv;
	while(fabs(adj) >= libxtide::Global::eventPrecisionJD) {
	  if (i++ == 12)
            return(-1.0e10); // Exceeded max iterations.
	  jdguess += adj;
          if (fabs (jdguess - jdorig) > 0.5)
            return (-1.0e10); // Ran out of bounds.
          alt2 = alt3;
	  alt3 = altitude (jdguess, lat, longit, lunar);
	  err = alt3 - alt;
  	  deriv = (alt3 - alt2) / adj;
          if (deriv == 0.0)
            return (-1.0e10); // Found dead end.
          adj = -err/deriv;
	}
	jdout = jdguess;

        // Figure out whether this is a rise or a set by shifting
        // by 1 second.
	{
          jdguess -= 1.0 / SEC_IN_DAY;
	  alt2 = altitude (jdguess, lat, longit, lunar);
          is_rise = (alt2 < alt3);
	}

	return(jdout);
}


// No important changes in Skycal V5.
static void flmoon(int n, int nph, double *jdout)

/* Gives jd (+- 2 min) of phase nph on lunation n; replaces
less accurate Numerical Recipes routine.  This routine
implements formulae found in Jean Meeus' *Astronomical Formulae
for Calculators*, 2nd edition, Willman-Bell.  A very useful
book!! */

{
  double jd, cor;
  double M, Mpr, F;
  double T;
  double lun;

  lun = (double) n + (double) nph / 4.;
  T = lun / 1236.85;
  jd = 2415020.75933 + 29.53058868 * lun
	  + 0.0001178 * T * T
	  - 0.000000155 * T * T * T
	  + 0.00033 * sin((166.56 + 132.87 * T - 0.009173 * T * T)/DEG_IN_RADIAN);
  M = 359.2242 + 29.10535608 * lun - 0.0000333 * T * T - 0.00000347 * T * T * T;
  M = M / DEG_IN_RADIAN;
  Mpr = 306.0253 + 385.81691806 * lun + 0.0107306 * T * T + 0.00001236 * T * T * T;
  Mpr = Mpr / DEG_IN_RADIAN;
  F = 21.2964 + 390.67050646 * lun - 0.0016528 * T * T - 0.00000239 * T * T * T;
  F = F / DEG_IN_RADIAN;
  if((nph == 0) || (nph == 2)) {/* new or full */
	  cor =   (0.1734 - 0.000393*T) * sin(M)
		  + 0.0021 * sin(2*M)
		  - 0.4068 * sin(Mpr)
		  + 0.0161 * sin(2*Mpr)
		  - 0.0004 * sin(3*Mpr)
		  + 0.0104 * sin(2*F)
		  - 0.0051 * sin(M + Mpr)
		  - 0.0074 * sin(M - Mpr)
		  + 0.0004 * sin(2*F+M)
		  - 0.0004 * sin(2*F-M)
		  - 0.0006 * sin(2*F+Mpr)
		  + 0.0010 * sin(2*F-Mpr)
		  + 0.0005 * sin(M+2*Mpr);
	  jd = jd + cor;
  }
  else {
	  cor = (0.1721 - 0.0004*T) * sin(M)
		  + 0.0021 * sin(2 * M)
		  - 0.6280 * sin(Mpr)
		  + 0.0089 * sin(2 * Mpr)
		  - 0.0004 * sin(3 * Mpr)
		  + 0.0079 * sin(2*F)
		  - 0.0119 * sin(M + Mpr)
		  - 0.0047 * sin(M - Mpr)
		  + 0.0003 * sin(2 * F + M)
		  - 0.0004 * sin(2 * F - M)
		  - 0.0006 * sin(2 * F + Mpr)
		  + 0.0021 * sin(2 * F - Mpr)
		  + 0.0003 * sin(M + 2 * Mpr)
		  + 0.0004 * sin(M - 2 * Mpr)
		  - 0.0003 * sin(2*M + Mpr);
	  if(nph == 1) cor = cor + 0.0028 -
			  0.0004 * cos(M) + 0.0003 * cos(Mpr);
	  if(nph == 3) cor = cor - 0.0028 +
			  0.0004 * cos(M) - 0.0003 * cos(Mpr);
	  jd = jd + cor;

  }
  *jdout = jd;
}


// Added 2003-02-04 from Skycal 5 for moonrise/moonset
static double circulo (double x) {
	/* assuming x is an angle in degrees, returns 
	   modulo 360 degrees. */

	int n;

	n = (int)(x / 360.);
	return(x - 360. * n);
}	


// Added 2003-02-04 from Skycal 5 for moonrise/moonset
static void geocent (double geolong, double geolat, double height,
		     double *x_geo, double *y_geo, double *z_geo)

/* computes the geocentric coordinates from the geodetic 
(standard map-type) longitude, latitude, and height. 
These are assumed to be in decimal hours, decimal degrees, and
meters respectively.  Notation generally follows 1992 Astr Almanac, 
p. K11 */

{
	
	double denom, C_geo, S_geo;

	geolat = geolat / DEG_IN_RADIAN;
	geolong = geolong / HRS_IN_RADIAN;      
	denom = (1. - FLATTEN) * sin(geolat);
	denom = cos(geolat) * cos(geolat) + denom*denom;
	C_geo = 1. / sqrt(denom);
	S_geo = (1. - FLATTEN) * (1. - FLATTEN) * C_geo;
	C_geo = C_geo + height / EQUAT_RAD;  /* deviation from almanac
                       notation -- include height here. */
	S_geo = S_geo + height / EQUAT_RAD;
	*x_geo = C_geo * cos(geolat) * cos(geolong);
	*y_geo = C_geo * cos(geolat) * sin(geolong);
	*z_geo = S_geo * sin(geolat);
}


// Added 2003-02-04 from Skycal 5 for moonrise/moonset
static void eclrot (double jd, double *x unusedParameter, double *y, double *z)

/* rotates ecliptic rectangular coords x, y, z to
   equatorial (all assumed of date.) */

{
	double incl;
	double ypr,zpr;
	double T;

	T = (jd - J2000) / 36525;  /* centuries since J2000 */
	
	incl = (23.439291 + T * (-0.0130042 - 0.00000016 * T))/DEG_IN_RADIAN; 
		/* 1992 Astron Almanac, p. B18, dropping the 
                   cubic term, which is 2 milli-arcsec! */
	ypr = cos(incl) * *y - sin(incl) * *z;
	zpr = sin(incl) * *y + cos(incl) * *z;
	*y = ypr;
	*z = zpr;
	/* x remains the same. */	
}


// Code by James.Ashton@anu.edu.au 2013-05-24
// Use the polynomial approximations from
//   http://eclipse.gsfc.nasa.gov/SEhelp/deltatpoly2004.html
// good for 1999 BC to 3000 AD, derived by NASA from
//   Morrison, L. and Stephenson, F. R., "Historical Values of the Earth's
//   Clock Error delt and the Calculation of Eclipses", J. Hist. Astron.,
//   Vol. 35 Part 3, August 2004, No. 120, pp 327-336 (2004).
static double etcorr (double jd) {
	double y = (jd - J2000) / GREG_DAYS_IN_YEAR + 2000;
	double t, delt;

	if (y < -500) {
		t = (y-1820)/100;
		delt = -20 + 32 * t * t;
	} else if (y < 500) {
		t = y/100;
		delt = 10583.6 - 1014.41 * t + 33.78311 * t * t - 5.952053 * t * t * t - 0.1798452 * t * t * t * t + 0.022174192 * t * t * t * t * t + 0.0090316521 * t * t * t * t * t * t;
	} else if (y < 1600) {
		t = (y-1000)/100;
		delt = 1574.2 - 556.01 * t + 71.23472 * t * t + 0.319781 * t * t * t - 0.8503463 * t * t * t * t - 0.005050998 * t * t * t * t * t + 0.0083572073 * t * t * t * t * t * t;
	} else if (y < 1700) {
		t = y - 1600;
		delt = 120 - 0.9808 * t - 0.01532 * t * t + t * t * t / 7129;
	} else if (y < 1800) {
		t = y - 1700;
		delt = 8.83 + 0.1603 * t - 0.0059285 * t * t + 0.00013336 * t * t * t - t * t * t * t / 1174000;
	} else if (y < 1860) {
		t = y - 1800;
		delt = 13.72 - 0.332447 * t + 0.0068612 * t * t + 0.0041116 * t * t * t - 0.00037436 * t * t * t * t + 0.0000121272 * t * t * t * t * t - 0.0000001699 * t * t * t * t * t * t + 0.000000000875 * t * t * t * t * t * t * t;
	} else if (y < 1900) {
		t = y - 1860;
		delt = 7.62 + 0.5737 * t - 0.251754 * t * t + 0.01680668 * t * t * t -0.0004473624 * t * t * t * t + t * t * t * t * t / 233174;
	} else if (y < 1920) {
		t = y - 1900;
		delt = -2.79 + 1.494119 * t - 0.0598939 * t * t + 0.0061966 * t * t * t - 0.000197 * t * t * t * t;
	} else if (y < 1941) {
		t = y - 1920;
		delt = 21.20 + 0.84493 * t - 0.076100 * t * t + 0.0020936 * t * t * t;
	} else if (y < 1961) {
		t = y - 1950;
		delt = 29.07 + 0.407 * t - t * t/233 + t * t * t / 2547;
	} else if (y < 1986) {
		t = y - 1975;
		delt = 45.45 + 1.067 * t - t * t/260 - t * t * t / 718;
	} else if (y < 2005) {
		t = y - 2000;
		delt = 63.86 + 0.3345 * t - 0.060374 * t * t + 0.0017275 * t * t * t + 0.000651814 * t * t * t * t + 0.00002373599 * t * t * t * t * t;
	} else if (y < 2050) {
		t = y - 2000;
		delt = 62.92 + 0.32217 * t + 0.005589 * t * t;
	} else if (y < 2150) {
		t = (y - 1820)/100;
		delt = -20 + 32 * t * t - 0.5628 * (2150 - y);
	} else {
		t = (y - 1820) / 100;
		delt = -20 + 32 * t * t;
	}

	return(jd + delt/SEC_IN_DAY);
}


// Added 2003-02-04 from Skycal 5 for moonrise/moonset
static void accumoon (double jd, double geolat, double lst, double elevsea,
double *topora, double *topodec, double *topodist)

// double jd,geolat,lst,elevsea;  /* jd, dec. degr., dec. hrs., meters */

/* More accurate (but more elaborate and slower) lunar 
   ephemeris, from Jean Meeus' *Astronomical Formulae For Calculators*,
   pub. Willman-Bell.  Includes all the terms given there. */

{	
/*	double *eclatit,*eclongit, *pie,*ra,*dec,*dist; geocent quantities,
		formerly handed out but not in this version */
	double pie, dist;  /* horiz parallax */
	double Lpr,M,Mpr,D,F,Om,T,Tsq,Tcb;
	double e,lambda,B,beta,om1,om2;
	double sinx, x, y, z, l, m, n;
	double x_geo, y_geo, z_geo;  /* geocentric position of *observer* */	

	jd = etcorr(jd);   /* approximate correction to ephemeris time */
	T = (jd - 2415020.) / 36525.;   /* this based around 1900 ... */
	Tsq = T * T;
	Tcb = Tsq * T;

	Lpr = 270.434164 + 481267.8831 * T - 0.001133 * Tsq 
			+ 0.0000019 * Tcb;
	M = 358.475833 + 35999.0498*T - 0.000150*Tsq
			- 0.0000033*Tcb;
	Mpr = 296.104608 + 477198.8491*T + 0.009192*Tsq 
			+ 0.0000144*Tcb;
	D = 350.737486 + 445267.1142*T - 0.001436 * Tsq
			+ 0.0000019*Tcb;
	F = 11.250889 + 483202.0251*T -0.003211 * Tsq 
			- 0.0000003*Tcb;
	Om = 259.183275 - 1934.1420*T + 0.002078*Tsq 
			+ 0.0000022*Tcb;

	Lpr = circulo(Lpr);
	Mpr = circulo(Mpr);	
	M = circulo(M);
	D = circulo(D);
	F = circulo(F);
	Om = circulo(Om);

	
	sinx =  sin((51.2 + 20.2 * T)/DEG_IN_RADIAN);
	Lpr = Lpr + 0.000233 * sinx;
	M = M - 0.001778 * sinx;
	Mpr = Mpr + 0.000817 * sinx;
	D = D + 0.002011 * sinx;
	
	sinx = 0.003964 * sin((346.560+132.870*T -0.0091731*Tsq)/DEG_IN_RADIAN);

	Lpr = Lpr + sinx;
	Mpr = Mpr + sinx;
	D = D + sinx;
	F = F + sinx;

	sinx = sin(Om/DEG_IN_RADIAN);
	Lpr = Lpr + 0.001964 * sinx;
	Mpr = Mpr + 0.002541 * sinx;
	D = D + 0.001964 * sinx;
	F = F - 0.024691 * sinx;
	F = F - 0.004328 * sin((Om + 275.05 -2.30*T)/DEG_IN_RADIAN);

	e = 1 - 0.002495 * T - 0.00000752 * Tsq;

	M = M / DEG_IN_RADIAN;   /* these will all be arguments ... */
	Mpr = Mpr / DEG_IN_RADIAN;
	D = D / DEG_IN_RADIAN;
	F = F / DEG_IN_RADIAN;

	lambda = Lpr + 6.288750 * sin(Mpr)
		+ 1.274018 * sin(2*D - Mpr)
		+ 0.658309 * sin(2*D)
		+ 0.213616 * sin(2*Mpr)
		- e * 0.185596 * sin(M) 
		- 0.114336 * sin(2*F)
		+ 0.058793 * sin(2*D - 2*Mpr)
		+ e * 0.057212 * sin(2*D - M - Mpr)
		+ 0.053320 * sin(2*D + Mpr)
		+ e * 0.045874 * sin(2*D - M)
		+ e * 0.041024 * sin(Mpr - M)
		- 0.034718 * sin(D)
		- e * 0.030465 * sin(M+Mpr)
		+ 0.015326 * sin(2*D - 2*F)
		- 0.012528 * sin(2*F + Mpr)
		- 0.010980 * sin(2*F - Mpr)
		+ 0.010674 * sin(4*D - Mpr)
		+ 0.010034 * sin(3*Mpr)
		+ 0.008548 * sin(4*D - 2*Mpr)
		- e * 0.007910 * sin(M - Mpr + 2*D)
		- e * 0.006783 * sin(2*D + M)
		+ 0.005162 * sin(Mpr - D);

		/* And furthermore.....*/

	lambda = lambda + e * 0.005000 * sin(M + D)
		+ e * 0.004049 * sin(Mpr - M + 2*D)
		+ 0.003996 * sin(2*Mpr + 2*D)
		+ 0.003862 * sin(4*D)
		+ 0.003665 * sin(2*D - 3*Mpr)
		+ e * 0.002695 * sin(2*Mpr - M)
		+ 0.002602 * sin(Mpr - 2*F - 2*D)
		+ e * 0.002396 * sin(2*D - M - 2*Mpr)
		- 0.002349 * sin(Mpr + D)
		+ e * e * 0.002249 * sin(2*D - 2*M)
		- e * 0.002125 * sin(2*Mpr + M)
		- e * e * 0.002079 * sin(2*M)
		+ e * e * 0.002059 * sin(2*D - Mpr - 2*M)
		- 0.001773 * sin(Mpr + 2*D - 2*F)
		- 0.001595 * sin(2*F + 2*D)
		+ e * 0.001220 * sin(4*D - M - Mpr)
		- 0.001110 * sin(2*Mpr + 2*F)
		+ 0.000892 * sin(Mpr - 3*D)
		- e * 0.000811 * sin(M + Mpr + 2*D)
		+ e * 0.000761 * sin(4*D - M - 2*Mpr)
		+ e * e * 0.000717 * sin(Mpr - 2*M)
		+ e * e * 0.000704 * sin(Mpr - 2 * M - 2*D)
		+ e * 0.000693 * sin(M - 2*Mpr + 2*D)
		+ e * 0.000598 * sin(2*D - M - 2*F)
		+ 0.000550 * sin(Mpr + 4*D)
		+ 0.000538 * sin(4*Mpr)
		+ e * 0.000521 * sin(4*D - M)
		+ 0.000486 * sin(2*Mpr - D);
	
/*		*eclongit = lambda;  */

	B = 5.128189 * sin(F)
		+ 0.280606 * sin(Mpr + F)
		+ 0.277693 * sin(Mpr - F)
		+ 0.173238 * sin(2*D - F)
		+ 0.055413 * sin(2*D + F - Mpr)
		+ 0.046272 * sin(2*D - F - Mpr)
		+ 0.032573 * sin(2*D + F)
		+ 0.017198 * sin(2*Mpr + F)
		+ 0.009267 * sin(2*D + Mpr - F)
		+ 0.008823 * sin(2*Mpr - F)
		+ e * 0.008247 * sin(2*D - M - F) 
		+ 0.004323 * sin(2*D - F - 2*Mpr)
		+ 0.004200 * sin(2*D + F + Mpr)
		+ e * 0.003372 * sin(F - M - 2*D)
		+ 0.002472 * sin(2*D + F - M - Mpr)
		+ e * 0.002222 * sin(2*D + F - M)
		+ e * 0.002072 * sin(2*D - F - M - Mpr)
		+ e * 0.001877 * sin(F - M + Mpr)
		+ 0.001828 * sin(4*D - F - Mpr)
		- e * 0.001803 * sin(F + M)
		- 0.001750 * sin(3*F)
		+ e * 0.001570 * sin(Mpr - M - F)
		- 0.001487 * sin(F + D)
		- e * 0.001481 * sin(F + M + Mpr)
		+ e * 0.001417 * sin(F - M - Mpr)
		+ e * 0.001350 * sin(F - M)
		+ 0.001330 * sin(F - D)
		+ 0.001106 * sin(F + 3*Mpr)
		+ 0.001020 * sin(4*D - F)
		+ 0.000833 * sin(F + 4*D - Mpr);
     /* not only that, but */
	B = B + 0.000781 * sin(Mpr - 3*F)
		+ 0.000670 * sin(F + 4*D - 2*Mpr)
		+ 0.000606 * sin(2*D - 3*F)
		+ 0.000597 * sin(2*D + 2*Mpr - F)
		+ e * 0.000492 * sin(2*D + Mpr - M - F)
		+ 0.000450 * sin(2*Mpr - F - 2*D)
		+ 0.000439 * sin(3*Mpr - F)
		+ 0.000423 * sin(F + 2*D + 2*Mpr)
		+ 0.000422 * sin(2*D - F - 3*Mpr)
		- e * 0.000367 * sin(M + F + 2*D - Mpr)
		- e * 0.000353 * sin(M + F + 2*D)
		+ 0.000331 * sin(F + 4*D)
		+ e * 0.000317 * sin(2*D + F - M + Mpr)
		+ e * e * 0.000306 * sin(2*D - 2*M - F)
		- 0.000283 * sin(Mpr + 3*F);
	
	om1 = 0.0004664 * cos(Om/DEG_IN_RADIAN);	
	om2 = 0.0000754 * cos((Om + 275.05 - 2.30*T)/DEG_IN_RADIAN);
	
	beta = B * (1. - om1 - om2);
/*      *eclatit = beta; */
	
	pie = 0.950724 
		+ 0.051818 * cos(Mpr)
		+ 0.009531 * cos(2*D - Mpr)
		+ 0.007843 * cos(2*D)
		+ 0.002824 * cos(2*Mpr)
		+ 0.000857 * cos(2*D + Mpr)
		+ e * 0.000533 * cos(2*D - M)
		+ e * 0.000401 * cos(2*D - M - Mpr)
		+ e * 0.000320 * cos(Mpr - M)
		- 0.000271 * cos(D)
		- e * 0.000264 * cos(M + Mpr)
		- 0.000198 * cos(2*F - Mpr)
		+ 0.000173 * cos(3*Mpr)
		+ 0.000167 * cos(4*D - Mpr)
		- e * 0.000111 * cos(M)
		+ 0.000103 * cos(4*D - 2*Mpr)
		- 0.000084 * cos(2*Mpr - 2*D)
		- e * 0.000083 * cos(2*D + M)
		+ 0.000079 * cos(2*D + 2*Mpr)
		+ 0.000072 * cos(4*D)
		+ e * 0.000064 * cos(2*D - M + Mpr)
		- e * 0.000063 * cos(2*D + M - Mpr)
		+ e * 0.000041 * cos(M + D)
		+ e * 0.000035 * cos(2*Mpr - M)
		- 0.000033 * cos(3*Mpr - 2*D)
		- 0.000030 * cos(Mpr + D)
		- 0.000029 * cos(2*F - 2*D)
		- e * 0.000029 * cos(2*Mpr + M)
		+ e * e * 0.000026 * cos(2*D - 2*M)
		- 0.000023 * cos(2*F - 2*D + Mpr)
		+ e * 0.000019 * cos(4*D - M - Mpr);

	beta = beta/DEG_IN_RADIAN;
	lambda = lambda/DEG_IN_RADIAN;
	l = cos(lambda) * cos(beta);	
	m = sin(lambda) * cos(beta);
	n = sin(beta);
	eclrot(jd,&l,&m,&n);
	
	dist = 1/sin((pie)/DEG_IN_RADIAN);
	x = l * dist;
	y = m * dist;
	z = n * dist;

/*	*ra = atan_circ(l,m) * DEG_IN_RADIAN;
	*dec = asin(n) * DEG_IN_RADIAN;        */

	geocent(lst,geolat,elevsea,&x_geo,&y_geo,&z_geo);
	
	x = x - x_geo;  /* topocentric correction using elliptical earth fig. */
	y = y - y_geo;
	z = z - z_geo;

	*topodist = sqrt(x*x + y*y + z*z);
	
	l = x / (*topodist);
	m = y / (*topodist);
	n = z / (*topodist);

	*topora = atan_circ(l,m) * HRS_IN_RADIAN;
	*topodec = asin(n) * DEG_IN_RADIAN; 
}


// This began as print_phase in skycalc.c.
// No important changes in Skycal V5.
static void
find_next_moon_phase (double &jd, int &phase) {
  double newjd, lastnewjd, nextjd;
  short kount=0;

  // Originally, there was no problem with getting snagged, but since
  // I introduced the roundoff error going back and forth with Timestamp,
  // now it's a problem.
  // Move ahead by 1 second to avoid snagging.
  jd += 1.0 / SEC_IN_DAY;

  // Find current lunation.  I doubled the safety margin since it
  // seemed biased for forwards search.  Backwards search has since
  // been deleted, but little reason to mess with it.
  int nlast = (int)((jd - 2415020.5) / 29.5307 - 2);

  flmoon(nlast,0,&lastnewjd);
  flmoon(++nlast,0,&newjd);
  while (newjd <= jd) {
    lastnewjd = newjd;
    flmoon(++nlast,0,&newjd);
    require (kount++ < 5); // Original limit was 35 (!)
  }

  // We might save some work here by estimating, i.e.:
  //   x = jd - lastnewjd;
  //   noctiles = (int)(x / 3.69134);  /* 3.69134 = 1/8 month; truncate. */
  // However....

  assert (lastnewjd <= jd && newjd > jd);
  phase = 1;
  // Lunation is lastnewjd's lunation
  flmoon (--nlast, phase, &nextjd);       // Phase = 1
  if (nextjd <= jd) {
    flmoon (nlast, ++phase, &nextjd);   // Phase = 2
    if (nextjd <= jd) {
      flmoon (nlast, ++phase, &nextjd); // Phase = 3
      if (nextjd <= jd) {
	phase = 0;
	nextjd = newjd;
      }
    }
  }
  jd = nextjd;
}


void libxtide::Skycal::findNextMoonPhase (Timestamp t,
					  TideEvent &tideEvent_out) {
  int phase;
  double jd (t.jd());
  find_next_moon_phase (jd, phase);
  tideEvent_out.eventTime = jd;
  switch (phase) {
  case 0:
    tideEvent_out.eventType = TideEvent::newmoon;
    break;
  case 1:
    tideEvent_out.eventType = TideEvent::firstquarter;
    break;
  case 2:
    tideEvent_out.eventType = TideEvent::fullmoon;
    break;
  case 3:
    tideEvent_out.eventType = TideEvent::lastquarter;
    break;
  default:
    assert (false);
  }
}


// Here's another opportunity for Jeff Dairiki to write a better root
// finder :-)
//
// jd_sun_alt needed good initial guesses to find sunrises and
// sunsets.  This was not a problem since good guesses were easy to
// come by.  The original skycalendar did this with estimates based on
// the local midnight:
//
//    jd = date_to_jd(date); /* local midnight */
//    jdmid = jd + zone(use_dst,stdz,jd,jdbdst,jdedst) / 24.;  /* corresponding ut */
//    stmid = lst(jdmid,longit);
//    lpsun(jdmid,&rasun,&decsun);
//    hasunset = ha_alt(decsun,lat,-(0.83+horiz));
//    jdsunset = jdmid + adj_time(rasun+hasunset-stmid)/24.; /* initial guess */
//    jdsunset = jd_sun_alt(-(0.83+horiz), jdsunset,lat,longit); /* refinement */
//    jdsunrise = jdmid + adj_time(rasun-hasunset-stmid)/24.;
//    jdsunrise = jd_sun_alt(-(0.83+horiz),jdsunrise,lat,longit);
//
// While efficient, this is an inconvenient way to go about it when
// I'm looking for the next event from time t, and don't even know
// when midnight is.  So I messed with jd_sun_alt to make it converge
// better from bad initial guesses, and substituted three bad guesses
// for one good one.  Normally, two would suffice, but I wanted to
// add a safety margin in case one of them happens to land at a point
// that nukes jd_sun_alt.

// 2003-02-04
// Expanded to handle moonrise/moonset as well.

// Set lunar to true for moonrise/set.
static void
find_next_rise_or_set (double &jd, double lat, double longit, bool lunar,
bool &is_rise) {
  // Move ahead by precision interval to avoid snagging.
  jd += libxtide::Global::eventPrecisionJD;

  double jdorig = jd;
  double inc = 1.0 / 6.0; // 4 hours

  // First we want to know what we are looking for.
  bool looking_for = (altitude (jdorig, lat, longit, lunar) < riseAltitude);

  // Now give it a decent try.  Because jd_alt is so unpredictable,
  // we can even find things out of order (which is one reason we need
  // to know what we're looking for).
  double jdlooper = jdorig;
  do {
    jd = jd_alt (riseAltitude, jdlooper, lat, longit, lunar, is_rise);
    jdlooper += inc;
  // Loop either on error return (which is a negative number), or if we
  // found an event in the wrong direction, or the wrong kind of event.
  } while ((jd < 0.0) ||
           (jd <= jdorig) ||
           (is_rise != looking_for));
}


void libxtide::Skycal::findNextRiseOrSet (Timestamp t,
					  const Coordinates &c,
					  RiseSetType riseSetType,
					  TideEvent &tideEvent_out) {
  assert (!(c.isNull()));
  bool isRise;
  double jd = t.jd();
  // skycal "longit" is measured in HOURS WEST, not degrees east.
  // (lat is unchanged)
  find_next_rise_or_set (jd,
                         c.lat(),
                         -(c.lng())/15.0,
                         (riseSetType==lunar),
                         isRise);
  tideEvent_out.eventTime = jd;
  if (isRise)
    tideEvent_out.eventType = ((riseSetType == lunar) ? TideEvent::moonrise
                                                      : TideEvent::sunrise);
  else
    tideEvent_out.eventType = ((riseSetType == lunar) ? TideEvent::moonset
                                                      : TideEvent::sunset);
}


// Simple question deserving a simple answer...
const bool libxtide::Skycal::sunIsUp (Timestamp t, const Coordinates &c) {
  assert (!(c.isNull()));
  return (altitude (t.jd(), c.lat(), -(c.lng())/15.0, 0) >= riseAltitude);
}


#ifdef EXPERIMENTAL_MOON_AGE_NOT_PHASE
void libxtide::Skycal::findNewMoons (Timestamp t,
				     Timestamp &prev_out,
				     Timestamp &next_out) {
  double jd(t.jd()), newjd, lastnewjd;
  short kount=0;
  // Duplicated from find_next_moon_phase "find current lunation" block.
  // IDK whether 29.5307 really wants to be 29.530588853?
  int nlast = (int)((jd - 2415020.5) / 29.5307 - 2);
  flmoon(nlast,0,&lastnewjd);
  flmoon(++nlast,0,&newjd);
  while (newjd <= jd) {
    lastnewjd = newjd;
    flmoon(++nlast,0,&newjd);
    require (kount++ < 5); // Original limit was 35 (!)
  }
  // And presto, we're done.
  prev_out = lastnewjd;
  next_out = newjd;
}
#endif
