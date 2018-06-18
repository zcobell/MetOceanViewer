#-------------------------------------------------
#
# Project created by QtCreator 2018-06-17T20:44:02
#
#-------------------------------------------------

QT      -= core

TARGET = tide
TEMPLATE = lib
CONFIG += staticlib

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        $$PWD/../../thirdparty/libtcd-2.2.7/bit_pack.c \
        $$PWD/../../thirdparty/libtcd-2.2.7/tide_db.c \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Amplitude.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Angle.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Banner.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Calendar.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormC.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormH.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormL.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormNotC.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormT.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/ClientSideFont.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Colors.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Constituent.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/ConstituentSet.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Coordinates.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/CurrentBearing.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Date.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/DStr.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Global.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Graph.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/HarmonicsFile.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/HarmonicsPath.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Interval.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/MetaField.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Nullable.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/NullableInterval.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/NullablePredictionValue.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Offsets.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/PixelatedGraph.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/PredictionValue.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/RGBGraph.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Settings.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Skycal.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Speed.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Station.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/StationIndex.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/StationRef.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/SubordinateStation.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/SVGGraph.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/TTYGraph.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/TideEvent.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/TideEventsOrganizer.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Timestamp.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Units.cc \
        $$PWD/../../thirdparty/xtide-2.15.1/libxtide/Year.cc

INCLUDEPATH += $$PWD/../../thirdparty/xtide-2.15.1/libxtide

win32{
  INCLUDEPATH += $$PWD/../../thirdparty/libtcd-2.2.7/VS

  DEFINES += HAVE_INTTYPES_H HAVE_IO_H HAVE_LIBTCD HAVE_LLROUND HAVE_MEMORY_H HAVE_PROCESS_H \
             HAVE_LIBTCD HAVE_LLROUND HAVE_MEMORY_H HAVE_PROCESS_H USE_PRAGMA_MESSAGE \
             HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRING_H HAVE_SYS_STAT_H HAVE_SYS_TYPES_H \
             STDC_HEADERS USE_PRAGMA_MESSAGE UseLocalFiles UseSemicolonPathsep X_DISPLAY_MISSING \
             acceptarg3_t=size_t xttpd_group=\"nobody\" xttpd_user=\"nobody\" VERSION=\"2.15.1\" \
             PACKAGE_VERSION=\"2.15.1\" PACKAGE_NAME=\"XTide\" \ PACKAGE_BUGREPORT=\"dave@flaterco.com\" \
             PACKAGE=\"xtide\"
}

unix{
  INCLUDEPATH += $$PWD/../../thirdparty/libtcd-2.2.7/UNX
  DEFINES += HAVE_DIRENT_H HAVE_DLFCN_H HAVE_GOOD_STRFTIME HAVE_INTTYPES_H HAVE_LANGINFO_H HAVE_LIBTCD \
           HAVE_LLROUND HAVE_MEMORY_H HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRINGS_H HAVE_STRING_H HAVE_SYSLOG_H HAVE_SYS_RESOURCE_H \
           HAVE_SYS_STAT_H HAVE_SYS_TYPES_H HAVE_UNISTD_H STDC_HEADERS X_DISPLAY_MISSING acceptarg3_t=socklen_t xttpd_group=\"nobody\" \
           xttpd_user=\"nobody\" VERSION=\"2.15.1\" PACKAGE_VERSION=\"2.15.1\" PACKAGE_NAME=\"XTide\" \
           PACKAGE_BUGREPORT=\"dave@flaterco.com\" PACKAGE=\"xtide\"
}

unix {
    target.path = /usr/lib
    INSTALLS += target
}


