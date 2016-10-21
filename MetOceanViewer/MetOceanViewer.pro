#-------------------------------GPL-------------------------------------#
#
# MetOceanViewer - A library for working with ADCIRC models
# Copyright (C) 2016  Zach Cobell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#-----------------------------------------------------------------------#


QT       += core gui webenginewidgets network xml charts printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOcean_Viewer
TEMPLATE = app

NEFISHOME=$$PWD/../thirdparty/nefis/

NEFISSOURCES += $$NEFISHOME/src/c2c.c \
                $$NEFISHOME/src/df.c  \
                $$NEFISHOME/src/er.c  \
                $$NEFISHOME/src/f2c.c \
                $$NEFISHOME/src/gp.c  \
                $$NEFISHOME/src/gt.c  \
                $$NEFISHOME/src/hs.c  \
                $$NEFISHOME/src/nefis_version.cpp \
                $$NEFISHOME/src/oc.c  \
                $$NEFISHOME/src/pt.c  \
                $$NEFISHOME/src/rt.c  \
                $$NEFISHOME/src/wl-xdr.c

NEFISHEADERS += $$NEFISHOME/include/btps.h \
                $$NEFISHOME/include/c2c.h \
                $$NEFISHOME/include/config.h \
                $$NEFISHOME/include/df.h \
                $$NEFISHOME/include/f2c.h \
                $$NEFISHOME/include/gp.h \
                $$NEFISHOME/include/gt.h \
                $$NEFISHOME/include/hs.h \
                $$NEFISHOME/include/nef-def.h \
                $$NEFISHOME/include/nef-tag.h \
                $$NEFISHOME/include/nef-xdf.h \
                $$NEFISHOME/include/nefis.h \
                $$NEFISHOME/include/nefis_version.h \
                $$NEFISHOME/include/oc.h \
                $$NEFISHOME/include/pt.h \
                $$NEFISHOME/include/resource.h \
                $$NEFISHOME/include/rt.h \
                $$NEFISHOME/include/wl-xdr.h

SOURCES += src/main.cpp\
    src/ui_hwm_tab.cpp \
    src/ui_noaa_tab.cpp \
    src/ui_usgs_tab.cpp \
    src/timeseries_add_data.cpp \
    src/ui_timeseries_tab.cpp \
    src/about_dialog.cpp \
    src/MetOceanViewer_main.cpp \
    src/setupMetOceanViewerUI.cpp \
    src/noaa.cpp \
    src/usgs.cpp \
    src/user_timeseries.cpp \
    src/hwm.cpp \
    src/keyhandler.cpp \
    src/mov_qchartview.cpp \
    src/update_dialog.cpp \
    src/ui_tab_xtide.cpp \
    src/xtide.cpp \
    src/javascriptAsyncReturn.cpp \
    src/imeds.cpp \
    src/imeds_station.cpp \
    src/mov_generic.cpp \
    src/mov_colors.cpp \
    src/mov_session.cpp \
    src/adcirc_station_output.cpp \
    src/mov_nefis.cpp 

HEADERS  += \
    version.h \
    src/timeseries_add_data.h \
    src/about_dialog.h \
    src/MetOceanViewer.h \
    src/noaa.h \
    src/mov_errors.h \
    src/mov_flags.h \ 
    src/usgs.h \
    src/user_timeseries.h \
    src/hwm.h \
    src/keyhandler.h \
    src/mov_qchartview.h \
    src/mov_qwebenginepage.h \
    src/update_dialog.h \
    src/xtide.h \
    src/javascriptAsyncReturn.h \
    src/imeds.h \
    src/imeds_station.h \
    src/mov_generic.h \
    src/mov_colors.h \
    src/mov_session.h \
    src/adcirc_station_output.h \
    src/mov_nefis.h \
    src/nefis_defines.h 

FORMS    += \
    ui/timeseries_add_data.ui \
    ui/about_dialog.ui \
    ui/MetOceanViewer_main.ui \
    ui/update_dialog.ui

OTHER_FILES +=

LIBS += -L$$OUT_PWD/../thirdparty/nefis -lnefis

#...Compiler dependent options
DEFINES += MOV_ARCH=\\\"$$QT_ARCH\\\" 

#...Location of the Nefis srcs
INCLUDEPATH += $$PWD/../thirdparty/nefis/include
INCLUDEPATH += $$PWD/src

#...Microsoft Visual C++ compilers
*msvc* {

#...Location of the netCDF srcs
INCLUDEPATH += $$PWD/../thirdparty/netcdf/include

contains(QT_ARCH, i386){

#...Microsoft Visual C++ 32-bit compiler
message("Using MSVC-32 bit compiler...")
LIBS += -L$$PWD/../thirdparty/netcdf/libs_vc32 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE +=
QMAKE_CXXFLAGS_DEBUG += -DEBUG

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"msvc\\\"

} else {

#...Microsoft Visual C++ 64-bit compiler
message("Using MSVC-64 bit compiler...")
LIBS += -L$$PWD/../thirdparty/netcdf/libs_vc64 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE +=
QMAKE_CXXFLAGS_DEBUG += -DEBUG

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"msvc\\\"
}

}

#...MinGW 32 bit compiler, throw error
win32-g++{
error("MinGW not compatible with this project. See the Qt documentation for QtWebEngine.")
}

#...Unix - We assume static library for NetCDF installed
#          in the system path already
unix:!macx{
LIBS += -lnetcdf

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE +=
QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"gpp\\\"
}

#...Mac - Assume static libs for netCDF in this path
#         This, obviously, will vary by the machine
#         the code is built on
macx{
LIBS += -L/Users/zcobell/Software/netCDF/lib -lnetcdf
INCLUDEPATH += /Users/zcobell/Software/netCDF/src
ICON = img/mov.icns

#...Files to be srcd in the bundle
XTIDEBIN.files = $$PWD/mov_libs/bin/tide \
                 $$PWD/mov_libs/bin/harmonics.tcd
XTIDEBIN.path  = Contents/MacOS/XTide/bin

XTIDELIB.files = $$PWD/mov_libs/lib/libtcd.dylib \
                 $$PWD/mov_libs/lib/libxtide.dylib
XTIDELIB.path  = Contents/MacOS/XTide/lib

QMAKE_BUNDLE_DATA += XTIDEBIN XTIDELIB

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE +=
QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"xcode\\\"
}

INCLUDEPATH += src

RESOURCES += \
    MetOceanViewer.qrc
    
RC_FILE = resources.rc

#...Ensure that git is in the system path. If not using GIT comment these two lines
GIT_VERSION = $$system(git --git-dir $$PWD/../.git --work-tree $$PWD/.. describe --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"
