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
# The name "MetOcean Viewer" is specific to this project and may not be
# used for projects "forked" or derived from this work.
#
#-----------------------------------------------------------------------#

QT       += core gui webenginewidgets network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOcean_Viewer
TEMPLATE = app

SOURCES += src/main.cpp\
    src/ui_hwm_tab.cpp \
    src/ui_noaa_tab.cpp \
    src/ui_usgs_tab.cpp \
    src/hwm_data.cpp \
    src/hwm_functions.cpp \
    src/usgs_functions.cpp \
    src/general_functions.cpp \
    src/timeseries_data.cpp \
    src/timeseries_functions.cpp \
    src/timeseries_add_data.cpp \
    src/ui_timeseries_tab.cpp \
    src/about_dialog.cpp \
    src/fileio.cpp \
    src/MetOceanViewer_main.cpp \
    src/setupMetOceanViewerUI.cpp \
    src/noaa/noaa.cpp \
    src/noaa/noaa_fetchNOAAData.cpp \
    src/noaa/noaa_formatNOAAResponse.cpp \
    src/noaa/noaa_plotNOAAResponse.cpp \
    src/noaa/noaa_readNOAAResponse.cpp \
    src/noaa/noaa_retrieveproduct.cpp \
    src/usgs/usgs.cpp \
    src/usgs/usgs_fetchUSGSData.cpp \
    src/usgs/usgs_formastUSGSInstantResponse.cpp \
    src/usgs/usgs_readUSGSDataFinished.cpp \
    src/usgs/usgs_formatUSGSDailyResponse.cpp \
    src/usgs/usgs_getTimezone.cpp \
    src/usgs/usgs_plotUSGS.cpp

HEADERS  += \
    include/hwm.h \
    include/timeseries_add_data.h \
    include/timeseries.h \
    include/about_dialog.h \
    version.h \
    include/MetOceanViewer.h \
    include/noaa.h \
    include/mov_errors.h \
    include/mov_flags.h \ 
    include/usgs.h

FORMS    += \
    ui/timeseries_add_data.ui \
    ui/about_dialog.ui \
    ui/MetOceanViewer_main.ui

OTHER_FILES +=

#...Location of the netCDF includes
INCLUDEPATH += $$PWD/thirdparty/netcdf/include

#...Compiler dependent options
DEFINES += MOV_ARCH=\\\"$$QT_ARCH\\\" EBUG

#...Microsoft Visual C++ compilers
*msvc* {

contains(QT_ARCH, i386){

#...Microsoft Visual C++ 32-bit compiler
message("Using MSVC-32 bit compiler...")
LIBS += -L$$PWD/thirdparty/netcdf/libs_vc32 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE +=
QMAKE_CXXFLAGS_DEBUG +=

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"msvc\\\"

} else {

#...Microsoft Visual C++ 64-bit compiler
message("Using MSVC-64 bit compiler...")
LIBS += -L$$PWD/thirdparty/netcdf/libs_vc64 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE +=
QMAKE_CXXFLAGS_DEBUG +=

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"msvc\\\"
}

}


#...MinGW 32 bit compiler
win32-g++{
message("Using MinGW-32 bit compiler...")

LIBS += -L$$PWD/thirdparty/netcdf/bin_32 -lnetcdf -lhdf5 -lz -lcurl

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"mingw\\\"
}

#...Unix - We assume static library for NetCDF installed
#          in the system path already
unix:!macx{
LIBS += -lnetcdf

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"gpp\\\"
}

#...Mac - Assume static libs for netCDF in this path
#         This, obviously, will vary by the machine
#         the code is built on
macx{
LIBS += -L/Users/zcobell/Software/netCDF/lib -lnetcdf
INCLUDEPATH += /Users/zcobell/Software/netCDF/include
ICON = img/mov.icns

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

#...Define a variable for the about dialog
DEFINES += MOV_COMPILER=\\\"xcode\\\"
}

INCLUDEPATH += include

RESOURCES += \
    MetOceanViewer.qrc

RC_FILE = resources.rc

#...Ensure that git is in the system path. If not using GIT comment these two lines
GIT_VERSION = $$system(git --git-dir $$PWD/.git --work-tree $$PWD describe --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"
