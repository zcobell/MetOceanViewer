QT  += core gui network xml charts printsupport
QT  += qml quick positioning location quickwidgets

include($$PWD/../global.pri)

INCLUDEPATH += $$PWD/../thirdparty/boost_1_67_0

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = MetOceanViewer2
TEMPLATE = app

CONFIG += c++11

debug{
CONFIG += sanitizer
CONFIG += sanitize_address
}

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    chartoptions.cpp \
    chartoptionsmenu.cpp \
    combobox.cpp \
    datebox.cpp \
    main.cpp \
    mainwindow.cpp \
    chartview.cpp \
    mapchartwidget.cpp \
    mapview.cpp \
    metocean.cpp \
    newtabdialog.cpp \
    noaaproductlist.cpp \
    noaatab.cpp \
    stationlist.cpp \
    stationmodel.cpp \
    mapfunctions.cpp \
    tabbar.cpp \
    tabwidget.cpp \
    usgstab.cpp

HEADERS += \
    chartoptions.h \
    chartoptionsmenu.h \
    combobox.h \
    datebox.h \
    mainwindow.h \
    mapchartwidget.h \
    chartview.h \
    mapview.h \
    metocean.h \
    newtabdialog.h \
    noaaproductlist.h \
    noaatab.h \
    stationlist.h \
    tabbar.h \
    tabtypes.h \
    stationmodel.h \
    mapfunctions.h \
    tabwidget.h \
    usgstab.h

FORMS += \
    chartoptions.ui \
    mainwindow.ui \
    newtabdialog.ui

RESOURCES += \
    resources.qrc

RC_FILE = resources.rc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

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
