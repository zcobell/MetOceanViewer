# $Id: Makefile.am 6280 2016-02-23 00:33:39Z flaterco $

bin_PROGRAMS    = tide
sbin_PROGRAMS   = xttpd
lib_LTLIBRARIES = libxtide.la
dist_man_MANS   = tide.1 xttpd.8
if HAVE_X
bin_PROGRAMS   += xtide
dist_man_MANS  += xtide.1
endif

AUTOMAKE_OPTIONS = subdir-objects
AM_CPPFLAGS     = @ADDED_CPPFLAGS@
AM_CXXFLAGS     = @X_CFLAGS@ @ADDED_CXXFLAGS@
BUILT_SOURCES   = libxtide/xml_y.h
AM_YFLAGS       = -d
ACLOCAL_AMFLAGS = -I m4
CLEANFILES      = INSTALL README ChangeLog NEWS AUTHORS README-QUICK
EXTRA_DIST	= CONFIGURE-HELP CodingConventions.txt README-QUICK \
		  scripts/xtide.conf.sample \
		  scripts/Fedora/rc.xttpd \
		  scripts/Fedora/xttpd-wrapper.sh \
		  scripts/Fedora/xttpd.conf \
		  scripts/DOS/Makefile.dj2 \
		  scripts/DOS/.xtide.xml \
		  scripts/DOS/autoconf-defines.h \
		  scripts/VS/NOTES.txt \
		  scripts/VS/autoconf-defines.h \
		  scripts/VS/build.bat \
		  scripts/Android/build.sh \
		  scripts/systemd/xttpd.service.in \
		  scripts/systemd/xttpd.socket \
		  iconsrc/Palette.ppm \
		  iconsrc/icon_16x16_3color.ico \
		  iconsrc/icon_16x16_3color.png \
		  iconsrc/icon_16x16_orig.png \
		  iconsrc/icon_48x48_3color.png \
		  iconsrc/icon_48x48_orig.png


libxtide_la_SOURCES = \
  libxtide/Amplitude.cc \
  libxtide/Angle.cc \
  libxtide/Banner.cc \
  libxtide/Calendar.cc \
  libxtide/CalendarFormC.cc \
  libxtide/CalendarFormH.cc \
  libxtide/CalendarFormL.cc \
  libxtide/CalendarFormNotC.cc \
  libxtide/CalendarFormT.cc \
  libxtide/ClientSideFont.cc \
  libxtide/Colors.cc \
  libxtide/Constituent.cc \
  libxtide/ConstituentSet.cc \
  libxtide/Coordinates.cc \
  libxtide/CurrentBearing.cc \
  libxtide/Date.cc \
  libxtide/Global.cc \
  libxtide/Graph.cc \
  libxtide/HarmonicsFile.cc \
  libxtide/HarmonicsPath.cc \
  libxtide/Interval.cc \
  libxtide/MetaField.cc \
  libxtide/Nullable.cc \
  libxtide/NullableInterval.cc \
  libxtide/NullablePredictionValue.cc \
  libxtide/Offsets.cc \
  libxtide/PixelatedGraph.cc \
  libxtide/PredictionValue.cc \
  libxtide/RGBGraph.cc \
  libxtide/Settings.cc \
  libxtide/Skycal.cc \
  libxtide/Speed.cc \
  libxtide/Station.cc \
  libxtide/StationIndex.cc \
  libxtide/StationRef.cc \
  libxtide/SubordinateStation.cc \
  libxtide/SVGGraph.cc \
  libxtide/TTYGraph.cc \
  libxtide/TideEvent.cc \
  libxtide/TideEventsOrganizer.cc \
  libxtide/Timestamp.cc \
  libxtide/Units.cc \
  libxtide/Year.cc \
  libxtide/xml_l.ll \
  libxtide/xml_y.yy
nobase_include_HEADERS = \
  libxtide/Amplitude.hh \
  libxtide/Angle.hh \
  libxtide/Banner.hh \
  libxtide/BetterMap.hh \
  libxtide/Calendar.hh \
  libxtide/CalendarFormC.hh \
  libxtide/CalendarFormH.hh \
  libxtide/CalendarFormL.hh \
  libxtide/CalendarFormNotC.hh \
  libxtide/CalendarFormT.hh \
  libxtide/ClientSideFont.hh \
  libxtide/Colors.hh \
  libxtide/Configurable.hh \
  libxtide/Constituent.hh \
  libxtide/ConstituentSet.hh \
  libxtide/Coordinates.hh \
  libxtide/CurrentBearing.hh \
  libxtide/Date.hh \
  libxtide/Errors.hh \
  libxtide/Global.hh \
  libxtide/Graph.hh \
  libxtide/HarmonicsFile.hh \
  libxtide/HarmonicsPath.hh \
  libxtide/Interval.hh \
  libxtide/MetaField.hh \
  libxtide/ModeFormat.hh \
  libxtide/Nullable.hh \
  libxtide/NullableInterval.hh \
  libxtide/NullablePredictionValue.hh \
  libxtide/Offsets.hh \
  libxtide/PixelatedGraph.hh \
  libxtide/PredictionValue.hh \
  libxtide/RGBGraph.hh \
  libxtide/SafeVector.hh \
  libxtide/Settings.hh \
  libxtide/Skycal.hh \
  libxtide/Speed.hh \
  libxtide/Station.hh \
  libxtide/StationIndex.hh \
  libxtide/StationRef.hh \
  libxtide/SubordinateStation.hh \
  libxtide/SVGGraph.hh \
  libxtide/TTYGraph.hh \
  libxtide/TideEvent.hh \
  libxtide/TideEventsOrganizer.hh \
  libxtide/Timestamp.hh \
  libxtide/Units.hh \
  libxtide/Year.hh \
  libxtide/libxtide.hh \
  libxtide/config.hh \
  libxtide/rgb.hh \
  libxtide/xmlparser.hh
nobase_nodist_include_HEADERS = libxtide/autoconf-defines.h

if NO_LIBDSTR
libxtide_la_SOURCES += libxtide/Dstr.cc
nobase_include_HEADERS += libxtide/Dstr.hh
endif

# -version-info current:revision:age

# Shared lib versions are almost, but not quite, completely unlike
# release versions.  They are sufficiently unlike that if the two
# happen to be the same it will cause people to jump to the conclusion
# that you don't know what you are doing.

# If the library source code has changed at all since the last update,
# then increment revision (c:r:a becomes c:r+1:a).

# If any interfaces have been added, removed, or changed since the
# last update, increment current, and set revision to 0.

# If any interfaces have been added since the last public release,
# then increment age.

# If any interfaces have been removed since the last public release,
# then set age to 0.

# FIXME: Update libtool version info on each release
libxtide_la_LDFLAGS = -version-info 1:1:0


if HAVE_X
xtide_SOURCES = \
  errorExclaim.xpm.hh errorSunk.xpm.hh icon_48x48_3color.xpm.hh          \
  wvsrtv.cc xtide.cc xtide.hh xxAspect.cc xxAspect.hh xxClock.cc         \
  xxClock.hh xxDisclaimer.cc xxDisclaimer.hh xxErrorBox.cc xxErrorBox.hh \
  xxFilename.cc xxFilename.hh xxGlobal.cc xxGlobal.hh xxGlobe.cc         \
  xxGlobe.hh xxGraphMode.cc xxGraphMode.hh xxHelpBox.cc xxHelpBox.hh     \
  xxHorizDialog.cc xxHorizDialog.hh xxLocationList.cc xxLocationList.hh  \
  xxMap.cc xxMap.hh xxMarkLevel.cc xxMarkLevel.hh                        \
  xxMouseWheelViewport.cc xxMouseWheelViewport.hh xxMultiChoice.cc       \
  xxMultiChoice.hh xxPixmapGraph.cc xxPixmapGraph.hh                     \
  xxPredictionWindow.cc xxPredictionWindow.hh xxRareModeSavePrompts.cc   \
  xxRareModeSavePrompts.hh xxReconfigurable.cc xxReconfigurable.hh       \
  xxRedrawable.cc xxRedrawable.hh xxRoot.cc xxRoot.hh xxSimplePrompt.cc  \
  xxSimplePrompt.hh xxStep.cc xxStep.hh xxTextMode.cc xxTextMode.hh      \
  xxTimestamp.cc xxTimestamp.hh xxTimestampDialog.cc                     \
  xxTimestampDialog.hh xxUnsignedChooser.cc xxUnsignedChooser.hh         \
  xxWidget.cc xxWidget.hh xxWindow.cc xxWindow.hh xxX.cc xxX.hh
xtide_LDFLAGS = @X_LIBS@
xtide_LDADD = libxtide.la @GPSD_LIBS@ -l@XAW@ -lXmu -lXt -lSM -lICE -lXext -lX11 -lXpm @X_EXTRA_LIBS@
endif


tide_SOURCES =  tide.cc
tide_LDADD = libxtide.la


xttpd_SOURCES = ZoneIndex.cc ZoneIndex.hh xttpd.cc
# xttpd needs -lsocket -lnsl on Solaris too.
xttpd_LDADD = @SYSTEMD_LIBS@ libxtide.la @X_EXTRA_LIBS@

install-systemd:
	install -o root -m 644 scripts/systemd/xttpd.service scripts/systemd/xttpd.socket /lib/systemd/system

enable-systemd:
	systemctl enable xttpd.socket xttpd.service
	systemctl start  xttpd.socket xttpd.service

barf-systemd:
	journalctl -b -p debug --no-pager


# 2016-01-14
# Using build scripts generated by autotools on Manjaro, regeneration of the
# XML parser fails because you get xml_y.hh instead of xml_y.h.  Using
# scripts generated on Slackware 14.1, regeneration succeeds on Manjaro.

xmlclean:
	rm -f libxtide/xml_l.cc libxtide/xml_y.cc libxtide/xml_y.h


docs:   INSTALL README ChangeLog NEWS AUTHORS README-QUICK
.PHONY: INSTALL README ChangeLog NEWS AUTHORS README-QUICK docs


# "Generic" INSTALL moved to CONFIGURE-HELP so that something more helpful
# can go here.
INSTALL:
	lynx -dump "http://www.flaterco.com/xtide/installation.html" > INSTALL


README:
	lynx -dump "http://www.flaterco.com/xtide/xtide.html" > README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/disclaimer.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/introduction.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/sysreq.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/installation.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/ports.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/modes.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/interactive.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/advanced.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/tty.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/xttpd.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/settings.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/harmonics.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/bugs.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/faq.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/design.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/bibliography.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/time_t.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/mincurrents.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/pound_to_fit.html" >> README
	echo "" >> README
	echo "################################################################" >> README
	echo "" >> README
	lynx -dump "http://www.flaterco.com/xtide/xtide1diff.html" >> README
	echo "" >> README


ChangeLog:
	lynx -dump "http://www.flaterco.com/xtide/changelog.html" > ChangeLog


NEWS:
	lynx -dump "http://www.flaterco.com/xtide/news.html" > NEWS


AUTHORS:
	lynx -dump "http://www.flaterco.com/xtide/credits.html" > AUTHORS


README-QUICK:
	lynx -dump "http://www.flaterco.com/xtide/quickinst.html" > README-QUICK
