REM $Id: build.bat 6280 2016-02-23 00:33:39Z flaterco $
REM Run this from VS2015 x64 Native Tools Command Prompt.

REM The distros for zlib, libpng, libtcd, and xtide must be unpacked and
REM ready to go.  Replace version numbers below as necessary.
SET ZVER=zlib-1.2.8
SET PVER=libpng-1.6.21
SET TVER=libtcd-2.2.7
SET XVER=xtide-2.15.1
SET XSTR=\"XTide 2.15.1 VS\"

REM libpng makefile expects to find ..\zlib
RENAME %ZVER% zlib

CD zlib
nmake /f win32\Makefile.msc zlib.lib
CD ..\%PVER%
nmake /f scripts\makefile.vcwin32 libpng.lib
CD ..\%TVER%
CALL VS\build.bat
CD ..\%XVER%

COPY scripts\VS\autoconf-defines.h libxtide
CD libxtide
CL /DPACKAGE_STRING="%XSTR%" /wd4530 /O2 /MD /analyze- /I. /I.. /I..\..\zlib /I..\..\%PVER% /I..\..\%TVER% /c Amplitude.cc Angle.cc Banner.cc Calendar.cc CalendarFormC.cc CalendarFormH.cc CalendarFormL.cc CalendarFormNotC.cc CalendarFormT.cc ClientSideFont.cc Colors.cc Constituent.cc ConstituentSet.cc Coordinates.cc CurrentBearing.cc Date.cc Global.cc Graph.cc HarmonicsFile.cc HarmonicsPath.cc Interval.cc MetaField.cc Nullable.cc NullableInterval.cc NullablePredictionValue.cc Offsets.cc PixelatedGraph.cc PredictionValue.cc RGBGraph.cc Settings.cc Skycal.cc Speed.cc Station.cc StationIndex.cc StationRef.cc SubordinateStation.cc SVGGraph.cc TTYGraph.cc TideEvent.cc TideEventsOrganizer.cc Timestamp.cc Units.cc Year.cc xml_l.cc xml_y.cc Dstr.cc
LIB /SUBSYSTEM:CONSOLE /OUT:xtide.lib Amplitude.obj Angle.obj Banner.obj Calendar.obj CalendarFormC.obj CalendarFormH.obj CalendarFormL.obj CalendarFormNotC.obj CalendarFormT.obj ClientSideFont.obj Colors.obj Constituent.obj ConstituentSet.obj Coordinates.obj CurrentBearing.obj Date.obj Global.obj Graph.obj HarmonicsFile.obj HarmonicsPath.obj Interval.obj MetaField.obj Nullable.obj NullableInterval.obj NullablePredictionValue.obj Offsets.obj PixelatedGraph.obj PredictionValue.obj RGBGraph.obj Settings.obj Skycal.obj Speed.obj Station.obj StationIndex.obj StationRef.obj SubordinateStation.obj SVGGraph.obj TTYGraph.obj TideEvent.obj TideEventsOrganizer.obj Timestamp.obj Units.obj Year.obj xml_l.obj xml_y.obj Dstr.obj
CD ..
CL /wd4530 /O2 /MD /analyze- /I. /I..\zlib /I..\%PVER% /I..\%TVER% tide.cc libxtide\xtide.lib ..\zlib\zlib.lib ..\%PVER%\libpng.lib ..\%TVER%\tcd.lib /Fetide /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /OPT:REF,ICF
