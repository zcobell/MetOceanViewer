#---- LGPL --------------------------------------------------------------------
#
# Copyright (C)  ARCADIS, 2011-2013.
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
# $Id$
# $HeadURL$
#------------------------------------------------------------------------------
#  File: ADCValidator.pro
#
#------------------------------------------------------------------------------

QT       += core gui webkitwidgets network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ADCValidator
TEMPLATE = app


SOURCES += src/main.cpp\
        src/ADCvalidator_main.cpp \
    src/ui_hwm_tab.cpp \
    src/ui_imeds_tab.cpp \
    src/ui_noaa_tab.cpp \
    src/imeds_data.cpp \
    src/hwm_data.cpp \
    src/imeds_functions.cpp \
    src/hwm_functions.cpp \
    src/noaa_functions.cpp \
    src/general_functions.cpp \
    src/add_imeds_data.cpp

HEADERS  += include/ADCvalidator.h \
    include/imeds.h \
    include/hwm.h \
    include/add_imeds_data.h

FORMS    += ui/ADCvalidator_main.ui \
    ui/add_imeds_data.ui

OTHER_FILES +=

#...Windows - We assume a folder that contains the pre-compiled
#             shared dll's from Unidata and another with the 
#             NetCDF header
win32{
LIBS += -LC:/netcdf/bin_qt -lnetcdf -lhdf5 -lz -lcurl
INCLUDEPATH += C:/netcdf/include
}

#...Unix - We assume static library for NetCDF installed
#          in the system path already
unix{
LIBS += -lnetcdf
}

INCLUDEPATH += include

RESOURCES += \
    ADCvalidatior.qrc
