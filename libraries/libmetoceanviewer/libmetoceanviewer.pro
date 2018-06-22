#-------------------------------------------------
#
# Project created by QtCreator 2018-06-13T16:37:33
#
#-------------------------------------------------

QT       += network positioning

TARGET = metoceanviewer
TEMPLATE = lib
CONFIG += staticlib

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS USE_PRAGMA_MESSAGE

INCLUDEPATH += $$PWD/../../thirdparty/boost_1_67_0
INCLUDEPATH += $$PWD/../../thirdparty/kdtree

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += ../../thirdparty/kdtree/kdtree2.cpp \
           hmdfasciiparser.cpp  \
           hmdf.cpp  \
           hmdfstation.cpp  \
           netcdftimeseries.cpp  \
           noaacoops.cpp  \
           qkdtree2.cpp  \
           stringutil.cpp  \
           timezone.cpp  \
           timezonestruct.cpp  \
           waterdata.cpp \
           station.cpp \ 
    usgswaterdata.cpp \
    xtidedata.cpp \
    tideprediction.cpp \
    ndbcdata.cpp

HEADERS += hmdfasciiparser.h  \
           hmdf.h  \
           hmdfstation.h  \
           netcdftimeseries.h  \
           noaacoops.h  \
           qkdtree2.h  \
           stringutil.h  \
           timezone.h  \
           timezonestruct.h  \
           tzdata.h  \
           waterdata.h \
           station.h \ 
    usgswaterdata.h \
    xtidedata.h \
    tideprediction.h \
    ndbcdata.h
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
}

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libtide/release/ -ltide
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libtide/debug/ -ltide
else:unix: LIBS += -L$$OUT_PWD/../libtide/ -ltide

INCLUDEPATH += $$PWD/../libtide
INCLUDEPATH += $$PWD/../../thirdparty/xtide-2.15.1/libxtide
DEPENDPATH += $$PWD/../libtide

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtide/release/libtide.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtide/debug/libtide.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtide/release/tide.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libtide/debug/tide.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libtide/libtide.a

RESOURCES += \
    resource_files.qrc
