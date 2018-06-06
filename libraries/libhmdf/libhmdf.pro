#-------------------------------GPL-------------------------------------#
#
# MetOcean Viewer - A simple interface for viewing hydrodynamic model data
# Copyright (C) 2015-2017  Zach Cobell
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

QT       += core positioning network

TARGET = hmdf
TEMPLATE = lib
CONFIG += staticlib

SOURCES += \
        hmdf.cpp \
    hmdfstation.cpp \
    stringutil.cpp \
    hmdfasciiparser.cpp \
    netcdftimeseries.cpp

HEADERS += \
        hmdf.h \
    hmdfstation.h \
    stringutil.h \
    hmdfasciiparser.h \
    netcdftimeseries.h
unix {
    target.path = /usr/lib
    INSTALLS += target
}


#...Microsoft Visual C++ compilers
*msvc* {

    #...Location of the netCDF srcs
    INCLUDEPATH += $$PWD/../../thirdparty/netcdf/include

    contains(QT_ARCH, i386){
        #...Microsoft Visual C++ 32-bit compiler
        message("Using MSVC-32 bit compiler...")
        LIBS += -L$$PWD/../../thirdparty/netcdf/libs_vc32 -lnetcdf -lhdf5 -lzlib -llibcurl_imp
    }else{

        #...Microsoft Visual C++ 64-bit compiler
        message("Using MSVC-64 bit compiler...")
        LIBS += -L$$PWD/../../thirdparty/netcdf/libs_vc64 -lnetcdf -lhdf5 -lzlib -llibcurl_imp
    }
}
#...Unix - We assume static library for NetCDF installed
#          in the system path already
unix:!macx{
    LIBS += -lnetcdf
}
#...Mac - Assume static libs for netCDF in this path
#         This, obviously, will vary by the machine
#         the code is built on
macx{
    LIBS += -L/Users/zcobell/Software/netCDF/lib -lnetcdf
    INCLUDEPATH += /Users/zcobell/Software/netCDF/src
    ICON = img/mov.icns
}


INCLUDEPATH += $$PWD/../../thirdparty/boost_1_67_0

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libtimezone/release/ -ltimezone
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libtimezone/debug/ -ltimezone
else:unix: LIBS += -L$$OUT_PWD/../libtimezone/ -ltimezone

INCLUDEPATH += $$PWD/../libtimezone
DEPENDPATH += $$PWD/../libtimezone

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtimezone/release/libtimezone.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtimezone/debug/libtimezone.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtimezone/release/timezone.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtimezone/debug/timezone.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libtimezone/libtimezone.a
