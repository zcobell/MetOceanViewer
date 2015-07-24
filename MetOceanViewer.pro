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

QT       += core gui webkitwidgets network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOcean_Viewer
TEMPLATE = app


SOURCES += src/main.cpp\
    src/ui_hwm_tab.cpp \
    src/ui_noaa_tab.cpp \
    src/ui_usgs_tab.cpp \
    src/hwm_data.cpp \
    src/hwm_functions.cpp \
    src/noaa_functions.cpp \
    src/usgs_functions.cpp \
    src/general_functions.cpp \
    src/timeseries_data.cpp \
    src/timeseries_functions.cpp \
    src/timeseries_add_data.cpp \
    src/ui_timeseries_tab.cpp \
    src/about_dialog.cpp \
    src/fileio.cpp \
    src/MetOceanViewer_main.cpp

HEADERS  += \
    include/hwm.h \
    include/timeseries_add_data.h \
    include/timeseries.h \
    include/about_dialog.h \
    version.h \
    include/MetOceanViewer.h

FORMS    += \
    ui/timeseries_add_data.ui \
    ui/about_dialog.ui \
    ui/MetOceanViewer_main.ui

OTHER_FILES +=

#...Windows - We assume a folder that contains the pre-compiled
#             shared dll's from Unidata and another with the 
#             NetCDF header
#         This, obviously, will vary by the machine
#         the code is built on
win32{
LIBS += -L$$PWD/libs -lnetcdf -lhdf5 -lz -lcurl
INCLUDEPATH += $$PWD/include
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
INCLUDEPATH += /Users/zcobell/Software/netCDF/include
ICON = img/mov.icns
}


INCLUDEPATH += include

RESOURCES += \
    MetOceanViewer.qrc

RC_FILE = resources.rc

#...Ensure that git is in the system path. If not using GIT comment these two lines
GIT_VERSION = $$system(git --git-dir $$PWD/.git --work-tree $$PWD describe --always --tags)
DEFINES += GIT_VERSION=\\\"$$GIT_VERSION\\\"

#...Optimization flags
QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG
