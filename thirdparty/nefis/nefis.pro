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


QT       -= core gui

TARGET = nefis
TEMPLATE = lib
CONFIG += staticlib

QMAKE_CFLAGS = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8 -DHAVE_CONFIG_H \
               -DLINUX64 -w

QMAKE_CXXFLAGS = -D_GNU_SOURCE -DSTR_LEN_AT_END -DFTN_UNDERSCORE \
               -D_FILE_OFFSET_BITS=64 -DPTR8 -DHAVE_CONFIG_H \
               -DLINUX64 -w

INCLUDEPATH += include

SOURCES += src/c2c.c \
           src/df.c  \
           src/er.c  \
           src/f2c.c \
           src/gp.c  \
           src/gt.c  \
           src/hs.c  \
           src/nefis_version.cpp \
           src/oc.c  \
           src/pt.c  \
           src/rt.c  \
           src/wl-xdr.c

HEADERS += include/btps.h \
           include/c2c.h \
           include/config.h \
           include/df.h \
           include/f2c.h \
           include/gp.h \
           include/gt.h \
           include/hs.h \
           include/nef-def.h \
           include/nef-tag.h \
           include/nef-xdf.h \
           include/nefis.h \
           include/nefis_version.h \
           include/oc.h \
           include/pt.h \
           include/resource.h \
           include/rt.h \
           include/wl-xdr.h
