#-------------------------------GPL-------------------------------------#
#
# MetOcean Viewer - A simple interface for viewing hydrodynamic model data
# Copyright (C) 2018  Zach Cobell
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

QT  += core gui network xml charts printsupport
QT  += qml quick positioning location quickwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOceanViewer
TEMPLATE = app

SOURCES +=\
    src/stationmodel.cpp \
    src/colors.cpp \
    src/dflow.cpp \
    src/errors.cpp \
    src/filetypes.cpp \
    src/generic.cpp \
    src/hwm.cpp \
    src/keyhandler.cpp \
    src/noaa.cpp \
    src/session.cpp \
    src/usgs.cpp \
    src/xtide.cpp \
    src/chartview.cpp \
    src/aboutdialog.cpp \
    src/adcircstationoutput.cpp \
    src/uihwmtab.cpp \
    src/uinoaatab.cpp \
    src/uitimeseriestab.cpp \
    src/uiusgstab.cpp \
    src/updatedialog.cpp \
    src/usertimeseries.cpp \
    src/mainwindow.cpp \
    src/main.cpp \
    src/addtimeseriesdialog.cpp \
    src/uixtidetab.cpp \
    src/mapfunctions.cpp \
    src/uindbctab.cpp \
    src/ndbc.cpp

HEADERS  += \
    src/metoceanviewer.h \
    src/stationmodel.h \
    src/colors.h \
    src/dflow.h \
    src/errors.h \
    src/filetypes.h \
    src/generic.h \
    src/hwm.h \
    src/keyhandler.h \
    src/noaa.h \
    src/session.h \
    src/usgs.h \
    src/xtide.h \
    src/chartview.h \
    src/aboutdialog.h \
    src/adcircstationoutput.h \
    src/addtimeseriesdialog.h \
    src/mainwindow.h \
    src/updatedialog.h \
    src/usertimeseries.h \
    version.h \
    src/mapfunctions.h \
    src/ndbc.h

FORMS    += \
    ui/aboutdialog.ui \
    ui/addtimeseriesdialog.ui \
    ui/updatedialog.ui \
    ui/mainwindow.ui

OTHER_FILES +=

#...Compiler dependent options
DEFINES += MOV_ARCH=\\\"$$QT_ARCH\\\" 

win32{
    DEFINES += USE_ANGLE
}

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


#...Unix - We assume static library for NetCDF installed
#          in the system path already
unix:!macx{
    LIBS += -lnetcdf

    #...Optimization flags
    QMAKE_CXXFLAGS_RELEASE +=
    QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

    #...The flag --no-pie is for linux systems to correctly
    #   recognize the output as an executable and not a shared lib
    #   Some versions of g++ don't require/have this option so check here
    #   for the configuration of the compiler
    GV = $$system(g++ --help --verbose 2>&1 >/dev/null | grep "enable-default-pie" | wc -l)
    greaterThan(GV, 0): QMAKE_LFLAGS += -no-pie

    #...Define a variable for the about dialog
    DEFINES += MOV_COMPILER=\\\"g++\\\"
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

DISTFILES += \
    qml/MovMapItem.qml \
    qml/MapViewer.qml \
    qml/InfoWindow.qml \
    qml/MapLegend.qml \
    qml/MapboxMapViewer.qml \
    qml/EsriMapViewer.qml \
    qml/OsmMapViewer.qml

#...Libraries
win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/release/ -lnetcdfcxx
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/debug/ -lnetcdfcxx
else:unix: LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/ -lnetcdfcxx

INCLUDEPATH += $$PWD/../libraries/libnetcdfcxx $$PWD/../thirdparty/netcdf-cxx/cxx4
DEPENDPATH += $$PWD/../libraries/libnetcdfcxx

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/release/libnetcdfcxx.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/debug/libnetcdfcxx.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/release/netcdfcxx.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/debug/netcdfcxx.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/libnetcdfcxx.a

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libproj4/release/ -lmovProj4
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libproj4/debug/ -lmovProj4
else:unix: LIBS += -L$$OUT_PWD/../libraries/libproj4/ -lmovProj4

INCLUDEPATH += $$PWD/../libraries/libproj4
DEPENDPATH += $$PWD/../libraries/libproj4

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/release/libmovProj4.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/debug/libmovProj4.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/release/movProj4.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/debug/movProj4.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/libmovProj4.a

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libmetoceanviewer/release/ -lmetoceanviewer
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libmetoceanviewer/debug/ -lmetoceanviewer
else:unix: LIBS += -L$$OUT_PWD/../libraries/libmetoceanviewer/ -lmetoceanviewer

INCLUDEPATH += $$PWD/../libraries/libmetoceanviewer
DEPENDPATH += $$PWD/../libraries/libmetoceanviewer

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/release/libmetoceanviewer.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/debug/libmetoceanviewer.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/release/metoceanviewer.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/debug/metoceanviewer.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/libmetoceanviewer.a

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libtide/release/ -ltide
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libtide/debug/ -ltide
else:unix: LIBS += -L$$OUT_PWD/../libraries/libtide/ -ltide

INCLUDEPATH += $$PWD/../libraries/libtide
DEPENDPATH += $$PWD/../libraries/libtide

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libtide/release/libtide.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libtide/debug/libtide.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libtide/release/tide.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libtide/debug/tide.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libtide/libtide.a
