#-------------------------------------------------
#
# Project created by QtCreator 2018-06-13T16:37:33
#
#-------------------------------------------------

QT       += network positioning

QT       -= gui

TARGET = noaacoops
TEMPLATE = lib
CONFIG += staticlib

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += $$PWD/../../thirdparty/boost_1_67_0

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        noaacoops.cpp

HEADERS += \
        noaacoops.h
unix {
    target.path = /usr/lib
    INSTALLS += target
}

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libhmdf/release/ -lhmdf
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libhmdf/debug/ -lhmdf
else:unix: LIBS += -L$$OUT_PWD/../libhmdf/ -lhmdf

INCLUDEPATH += $$PWD/../libhmdf
DEPENDPATH += $$PWD/../libhmdf

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libhmdf/release/libhmdf.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libhmdf/debug/libhmdf.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libhmdf/release/hmdf.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libhmdf/debug/hmdf.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libhmdf/libhmdf.a

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
