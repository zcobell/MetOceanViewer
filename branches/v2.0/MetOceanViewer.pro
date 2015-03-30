#---- LGPL --------------------------------------------------------------------
#
# Copyright (C)  ARCADIS, 2011-2015.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# contact: Zachary Cobell, zachary.cobell@arcadis-us.com
# ARCADIS
# 4999 Pearl East Circle, Suite 200
# Boulder, CO 80301
#
# All indications and logos of, and references to, "ARCADIS"
# are registered trademarks of ARCADIS, and remain the property of
# ARCADIS. All rights reserved.
#
#------------------------------------------------------------------------------
# $Author$
# $Date$
# $Rev$
# $HeadURL$
#------------------------------------------------------------------------------
#  File: ADCValidator.pro
#
#------------------------------------------------------------------------------

QT       += core gui webkitwidgets network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOceanViewer
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
LIBS += -LC:/netcdf/bin_qt -lnetcdf -lhdf5 -lz -lcurl
INCLUDEPATH += C:/netcdf/include 
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
ICON = img/icon.icns
}


INCLUDEPATH += include

RESOURCES += \
    MetOceanViewer.qrc

RC_FILE = resources.rc

QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_DEBUG += -O0

#...Gets the SVN Revision and appends it to the versioning in the code
VSN = $$system(svnversion)
VERSTR = '\\"$${VSN}\\"'  # place quotes around the version string
DEFINES += SVER=\"$${VERSTR}\" # create a VER macro containing the version string