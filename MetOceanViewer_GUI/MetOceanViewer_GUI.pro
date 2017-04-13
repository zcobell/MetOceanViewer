#-------------------------------GPL-------------------------------------#
#
# MetOcean Viewer - A simple interface for viewing hydrodynamic model data
# Copyright (C) 2015  Zach Cobell
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

TARGET = MetOceanViewer
TEMPLATE = app

SOURCES +=\
    src/movAboutDialog.cpp \
    src/movAdcircStationOutput.cpp \
    src/movAddTimeseriesData.cpp \
    src/movColors.cpp \
    src/movDflow.cpp \
    src/movFiletypes.cpp \
    src/movGeneric.cpp \
    src/movHwm.cpp \
    src/movImeds.cpp \
    src/movImedsStation.cpp \
    src/movInitUI.cpp \
    src/movJavascriptAsyncReturn.cpp \
    src/movKeyhandler.cpp \
    src/movMain.cpp \
    src/movNoaa.cpp \
    src/movQChartView.cpp \
    src/movSession.cpp \
    src/movStartup.cpp \
    src/movUserTimeseries.cpp \
    src/movUiHwmTab.cpp \
    src/movUiNoaaTab.cpp \
    src/movUiTimeseriesTab.cpp \
    src/movUiUsgsTab.cpp \
    src/movUiXTideTab.cpp \
    src/movUpdateDialog.cpp \
    src/movUsgs.cpp \
    src/movXtide.cpp \
    src/movErrors.cpp \
    src/movusertimeseriesoptions.cpp

HEADERS  += \
    src/movAdcircStationOutput.h \
    src/movColors.h \
    src/movDflow.h \
    src/movErrors.h \
    src/movFiletypes.h \
    src/movFlags.h \
    src/movGeneric.h \
    src/movHwm.h \
    src/movImeds.h \
    src/movImedsStation.h \
    src/movJavascriptAsyncReturn.h \
    src/movKeyhandler.h \
    src/movNoaa.h \
    src/movQChartView.h \
    src/movQWebEnginePage.h \
    src/movSession.h \
    src/movUserTimeseries.h \
    src/movUsgs.h \
    src/movXtide.h \
    movVersion.h \
    src/mov_dialog_addtimeseriesdata.h \
    src/mov_dialog_about.h \
    src/mov_dialog_update.h \
    src/movErrors.h \
    src/movusertimeseriesoptions.h \
    src/MainWindow.h

FORMS    += \
    ui/mov_dialog_about.ui \
    ui/mov_dialog_addtimeseries.ui \
    ui/mov_window_main.ui \
    ui/mov_dialog_update.ui \
    ui/movusertimeseriesoptions.ui

OTHER_FILES +=

#...Include the PROJ4 library
INCLUDEPATH += $$PWD/../libraries/libproj4
win32{
    CONFIG(debug, debug | release):LIBS += -L$$OUT_PWD/../libraries/libproj4/debug -lmovProj4
    CONFIG(release, debug | release):LIBS += -L$$OUT_PWD/../libraries/libproj4/release -lmovProj4
}
unix{
    LIBS += -L$$OUT_PWD/../libraries/libproj4 -lmovProj4
}

#...Include the netCDF4-CXX library
INCLUDEPATH += $$PWD/../thirdparty/netcdf-cxx/cxx4
win32{
    CONFIG(debug,debug|release):LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/debug -lnetcdfcxx
    CONFIG(release,debug|release):LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/release -lnetcdfcxx
}
unix{
    LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx -lnetcdfcxx
}

#...Include the KDTREE2 library
INCLUDEPATH += $$PWD/../libraries/libkdtree2
win32{
    CONFIG(debug,debug|release):LIBS += -L$$OUT_PWD/../libraries/libkdtree2/debug -lmovKdtree2
    CONFIG(release,debug|release):LIBS += -L$$OUT_PWD/../libraries/libkdtree2/release -lmovKdtree2
}
unix{
    LIBS += -L$$OUT_PWD/../libraries/libkdtree2 -lmovKdtree2
}

#...Compiler dependent options
DEFINES += MOV_ARCH=\\\"$$QT_ARCH\\\" 

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
    }else{

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
    movResource.qrc
    
RC_FILE = resources.rc

#...Ensure that git is in the system path. If not using GIT comment these two lines
GIT_VERSION = $$system(git --git-dir $$PWD/../.git --work-tree $$PWD/.. describe --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"
