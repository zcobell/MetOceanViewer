# Library version
VERSION=4.0.0

# If there is no version tag in git this one will be used
MOV_VERSION = \"unversioned\"

# Need to discard STDERR so get path to NULL device
win32 {
    NULL_DEVICE = NUL # Windows doesn't have /dev/null but has NUL
} else {
    NULL_DEVICE = /dev/null
}

# Need to call git with manually specified paths to repository
BASE_GIT_COMMAND = git --git-dir $$PWD/.git --work-tree $$PWD

# Trying to get version from git tag / revision
GIT_VERSION = $$system($$BASE_GIT_COMMAND describe --always --tags 2> $$NULL_DEVICE)

# Now we are ready to pass parsed version to Qt
!isEmpty(GIT_VERSION){
  MOV_VERSION = $$GIT_VERSION
}

# Adding C preprocessor #DEFINE so we can use it in C++ code
# also here we want full version on every system so using GIT_VERSION
DEFINES += GIT_VERSION=\\\"$$MOV_VERSION\\\"

# By default Qt only uses major and minor version for Info.plist on Mac.
# This will rewrite Info.plist with full version
macx {
    INFO_PLIST_PATH = $$shell_quote($${OUT_PWD}/$${TARGET}.app/Contents/Info.plist)
    QMAKE_POST_LINK += /usr/libexec/PlistBuddy -c \"Set :CFBundleShortVersionString $${MOV_VERSION}\" $${INFO_PLIST_PATH}
}

message("Current version: $$MOV_VERSION")
    
!isEmpty(NETCDFHOME){
    INCLUDEPATH += $$NETCDFHOME/include
    LIBS += -L$$NETCDFHOME/lib
}

#...Compiler dependent options
DEFINES += MOV_ARCH=\\\"$$QT_ARCH\\\"

win32{
    DEFINES += USE_ANGLE
}

#...Microsoft Visual C++ compilers
*msvc* {

    #...Location of the netCDF srcs
    INCLUDEPATH += $$PWD/thirdparty/netcdf/include

    contains(QT_ARCH, i386){

        #...Microsoft Visual C++ 32-bit compiler
        LIBS += -L$$PWD/thirdparty/netcdf/libs_vc32 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

        #...Optimization flags
        QMAKE_CXXFLAGS_RELEASE +=
        QMAKE_CXXFLAGS_DEBUG += -DEBUG

        #...Define a variable for the about dialog
        DEFINES += MOV_COMPILER=\\\"msvc\\\"
    }else{

        #...Microsoft Visual C++ 64-bit compiler
        LIBS += -L$$PWD/thirdparty/netcdf/libs_vc64 -lnetcdf -lhdf5 -lzlib -llibcurl_imp

        #...Optimization flags
        QMAKE_CXXFLAGS_RELEASE +=
        QMAKE_CXXFLAGS_DEBUG += -DEBUG

        #...Define a variable for the about dialog
        DEFINES += MOV_COMPILER=\\\"msvc\\\"
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

    #...Define a variable for the about dialog
    DEFINES += MOV_COMPILER=\\\"g++\\\"
}

#...Mac - Assume static libs for netCDF in this path
#         This, obviously, will vary by the machine
#         the code is built on
macx{
    LIBS += -L/Users/zcobell/Software/netCDF/lib -lnetcdf
    INCLUDEPATH += /Users/zcobell/Software/netCDF/src
    ICON = img/mov.icns

    #...Optimization flags
    QMAKE_CXXFLAGS_RELEASE +=
    QMAKE_CXXFLAGS_DEBUG += -O0 -DEBUG

    #...Define a variable for the about dialog
    DEFINES += MOV_COMPILER=\\\"xcode\\\"
}
