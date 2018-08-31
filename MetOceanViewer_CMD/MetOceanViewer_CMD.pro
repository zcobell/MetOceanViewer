QT += network positioning
QT -= gui

include($$PWD/../global.pri)

CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = MetOceanData

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        main.cpp \
    driver.cpp

INCLUDEPATH += ../

#...Microsoft Visual C++ compilers
*msvc* {

    #...Location of the netCDF srcs
    INCLUDEPATH += $$PWD/../thirdparty/netcdf/include

    contains(QT_ARCH, i386){

        #...Microsoft Visual C++ 32-bit compiler
        LIBS += -L$$PWD/../thirdparty/netcdf/libs_vc32 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

        #...Optimization flags
        QMAKE_CXXFLAGS_RELEASE +=
        QMAKE_CXXFLAGS_DEBUG += -DEBUG

    }else{

        #...Microsoft Visual C++ 64-bit compiler
        LIBS += -L$$PWD/../thirdparty/netcdf/libs_vc64 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

        #...Optimization flags
        QMAKE_CXXFLAGS_RELEASE +=
        QMAKE_CXXFLAGS_DEBUG += -DEBUG
    }

}


#...Unix - We assume static library for NetCDF installed
#          in the system path already
unix:!macx{
    LIBS += -lnetcdf

    #...Optimization flags
    QMAKE_CXXFLAGS_RELEASE +=
    QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

    #...The flag --no-pie is for linux systems to correctly
    #   recognize the output as an executable and not a shared lib
    #   Some versions of g++ don't require/have this option so check here
    #   for the configuration of the compiler
    GV = $$system(g++ --help --verbose 2>&1 >/dev/null | grep "enable-default-pie" | wc -l)
    greaterThan(GV, 0): QMAKE_LFLAGS += -no-pie

}

#...Mac - Assume static libs for netCDF in this path
#         This, obviously, will vary by the machine
#         the code is built on
macx{
    LIBS += -L/Users/zcobell/Software/netCDF/lib -lnetcdf
    INCLUDEPATH += /Users/zcobell/Software/netCDF/src

    #...Optimization flags
    QMAKE_CXXFLAGS_RELEASE +=
    QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG
}

HEADERS += \
    driver.h

win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libmetoceanviewer/release/ -lmetoceanviewer
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libmetoceanviewer/debug/ -lmetoceanviewer
else:unix: LIBS += -L$$OUT_PWD/../libraries/libmetoceanviewer/ -lmetoceanviewer

INCLUDEPATH += $$PWD/../libraries/libmetoceanviewer
DEPENDPATH += $$PWD/../libraries/libmetoceanviewer

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/release/libmetoceanviewer.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/debug/libmetoceanviewer.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/release/metoceanviewer.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/debug/metoceanviewer.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libmetoceanviewer/libmetoceanviewer.a


win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/release/ -lnetcdfcxx -lnetcdf
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/debug/ -lnetcdfcxx -lnetcdf
else:unix: LIBS += -L$$OUT_PWD/../libraries/libnetcdfcxx/ -lnetcdfcxx -lnetcdf

INCLUDEPATH += $$PWD/../libraries/libnetcdfcxx
DEPENDPATH += $$PWD/../libraries/libnetcdfcxx

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/release/libnetcdfcxx.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/debug/libnetcdfcxx.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/release/netcdfcxx.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/debug/netcdfcxx.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libnetcdfcxx/libnetcdfcxx.a


win32:CONFIG(release, debug|release): LIBS += -L$$OUT_PWD/../libraries/libproj4/release/ -lmovProj4
else:win32:CONFIG(debug, debug|release): LIBS += -L$$OUT_PWD/../libraries/libproj4/debug/ -lmovProj4
else:unix: LIBS += -L$$OUT_PWD/../libraries/libproj4/ -lmovProj4

INCLUDEPATH += $$PWD/../libraries/libproj4
DEPENDPATH += $$PWD/../libraries/libproj4

win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/release/libmovProj4.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/debug/libmovProj4.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/release/movProj4.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/debug/movProj4.lib
else:unix: PRE_TARGETDEPS += $$OUT_PWD/../libraries/libproj4/libmovProj4.a


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
