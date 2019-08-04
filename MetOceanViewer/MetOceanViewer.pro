#-------------------------------GPL-------------------------------------#
#
# MetOcean Viewer - A simple interface for viewing hydrodynamic model data
# Copyright (C) 2019  Zach Cobell
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

include($$PWD/../global.pri)

INCLUDEPATH += $$PWD/../thirdparty/boost_1_67_0

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOceanViewer
TEMPLATE = app

SOURCES +=\
    src/crms.cpp \
    src/crmsdialog.cpp \
    src/stationmodel.cpp \
    src/colors.cpp \
    src/dflow.cpp \
    src/errors.cpp \
    src/filetypes.cpp \
    src/hwm.cpp \
    src/keyhandler.cpp \
    src/noaa.cpp \
    src/session.cpp \
    src/uicrmstab.cpp \
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
    src/crms.h \
    src/crmsdialog.h \
    src/metoceanviewer.h \
    src/stationmodel.h \
    src/colors.h \
    src/dflow.h \
    src/errors.h \
    src/filetypes.h \
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
    src/mapfunctions.h \
    src/ndbc.h

FORMS    += \
    ui/crmsdialog.ui \
    ui/aboutdialog.ui \
    ui/addtimeseriesdialog.ui \
    ui/updatedialog.ui \
    ui/mainwindow.ui

OTHER_FILES +=

INCLUDEPATH += ../

INCLUDEPATH += src

RESOURCES += \
    movResource.qrc
    
RC_FILE = resources.rc

DISTFILES += \
    qml/MapboxGLMapViewer.qml \
    qml/MovMapItem.qml \
    qml/MapViewer.qml \
    qml/InfoWindow.qml \
    qml/MapLegend.qml \
    qml/MapboxMapViewer.qml \
    qml/EsriMapViewer.qml \
    qml/OsmMapViewer.qml

#...Libraries
win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libmetocean/release/ -lmetocean
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libmetocean/debug/ -lmetocean
else:unix: LIBS += -L$$OUT_PWD/../libraries/libmetocean/ -lmetocean

INCLUDEPATH += $$PWD/../libraries/libmetocean
DEPENDPATH += $$PWD/../libraries/libmetocean

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetocean/release/libmetocean.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetocean/debug/libmetocean.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetocean/release/metocean.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetocean/debug/metocean.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetocean/libmetocean.a

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


win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../thirdparty/ezproj/src/release/ -lezproj
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../thirdparty/ezproj/src/debug/ -lezproj
else:unix: LIBS += -L$$OUT_PWD/../thirdparty/ezproj/src/ -lezproj

INCLUDEPATH += $$PWD/../thirdparty/ezproj/src
DEPENDPATH += $$PWD/../thirdparty/ezproj/src

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../thirdparty/ezproj/src/release/libezproj.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../thirdparty/ezproj/src/debug/libezproj.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../thirdparty/ezproj/src/release/ezproj.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../thirdparty/ezproj/src/debug/ezproj.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../thirdparty/ezproj/src/libezproj.a

unix|win32: LIBS += -lnetcdf
