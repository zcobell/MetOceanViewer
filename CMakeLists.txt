# ##############################################################################
# CMake Build File for MetOceanViewer
#
# Written By: Zach Cobell
#
# ##############################################################################
#
# The CMake build system enable MetOceanViewer to be deployed and built in a
# cross platform environment.
#
# ##############################################################################

# ...Set the default build type
if (DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
            ${CMAKE_BUILD_TYPE}
            CACHE STRING "Choose the type of
        build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug
        Release RelWithDebInfo MinSizeRel.")
elseif (COVERAGE)
    set(CMAKE_BUILD_TYPE
            Debug
            CACHE
            STRING
            "Choose the type of build,
            options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release
            RelWithDebInfo MinSizeRel.")
else ()
    set(CMAKE_BUILD_TYPE
            Release
            CACHE STRING "Choose the type of build,
        options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release
        RelWithDebInfo MinSizeRel.")
endif ()

# ##############################################################################
# GENERAL OPTIONS
# ##############################################################################
cmake_minimum_required(VERSION 2.8.12)
cmake_policy(SET CMP0048 NEW)
project(MetOceanViewer VERSION 5.0.0)
enable_language(C)
enable_language(CXX)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckFunctionExists)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
# ##############################################################################

# ##############################################################################
# C++ 11/14 Check
# ##############################################################################
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
if (COMPILER_SUPPORTS_CXX17)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
else (COMPILER_SUPPORTS_CXX17)
    message(
            FATAL_ERROR
            "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support. Please use a different C++ compiler."
    )
endif (COMPILER_SUPPORTS_CXX17)
# ##############################################################################

# ##############################################################################
# Compiler flags
# ##############################################################################
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
mark_as_advanced(CLEAR CMAKE_CXX_FLAGS_RELEASE)
mark_as_advanced(CLEAR CMAKE_CXX_FLAGS_DEBUG)
mark_as_advanced(CLEAR CMAKE_C_FLAGS_RELEASE)
mark_as_advanced(CLEAR CMAKE_C_FLAGS_DEBUG)
mark_as_advanced(CLEAR CMAKE_CXX_COMPILER)
mark_as_advanced(CLEAR CMAKE_C_COMPILER)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/CMakeModules)
option(BUILD_SHARED_LIBMETOCEAN OFF "Build libmetocean as a shared library")
# ##############################################################################

# ##############################################################################
# LIBRARY VERSION
# ##############################################################################
set(METOCEANVIEWER_VERSION_MAJOR 0)
set(METOCEANVIEWER_VERSION_MINOR 3)
set(METOCEANVIEWER_VERSION_PATCH 0)
set(METOCEANVIEWER_VERSION_STRING
        ${METOCEANVIEWER_VERSION_MAJOR}.${METOCEANVIEWER_VERSION_MINOR}.${METOCEANVIEWER_VERSION_PATCH}
        )
# ##############################################################################

# ##############################################################################
# CODE VERSION (GIT)
# ##############################################################################
execute_process(
        COMMAND git describe --always --tags
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        RESULT_VARIABLE GIT_RETURN_VALUE
        OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT "${GIT_RETURN_VALUE}" STREQUAL "0")
    set(GIT_VERSION "${METOCEANVIEWER_VERSION_STRING}.cv")
endif ()
message(STATUS "MetOceanViewer Version: ${GIT_VERSION}")
# ##############################################################################

# ##############################################################################
# Boost
# ##############################################################################
find_package(Boost 1.60.0)
if (NOT Boost_FOUND OR PACKAGE_VERSION_UNSUITABLE)
    message(STATUS "Using included Boost Version: 1.66.0")
    set(Boost_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/boost_1_66_0)
endif ()
# ##############################################################################

# ##############################################################################
# SET THE LOCATION OF TEMPORARY STATIC LIBS
# ##############################################################################
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/CMakeFiles)
# ##############################################################################

# ##############################################################################
# NETCDF
# ##############################################################################
if (WIN32)
    set(NETCDF_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/thirdparty/netcdf/include")
else (WIN32)
    if (NOT "${NETCDFHOME}" STREQUAL "")
        set(NETCDF_DIR
                "${NETCDFHOME}"
                CACHE STRING "Location of NETCDF Library")
    elseif (NOT $ENV{NETCDFHOME} STREQUAL "")
        set(NETCDF_DIR
                $ENV{NETCDFHOME}
                CACHE STRING "Locatin of NETCDF Library")
    endif (NOT "${NETCDFHOME}" STREQUAL "")

    find_package(NetCDF)

    set(NETCDF_AdditionalLibs
            ""
            CACHE STRING "Additional libraries that may be required for netCDF")

    if (NOT NETCDF_FOUND)
        message(SEND_ERROR "Specify the netCDF path on the following screen")
    else (NOT NETCDF_FOUND)

        set(netcdf_c_code
                "
		#include <netcdf.h>
		int main(){
			int ncid,varid;
			int ierr = nc_def_var_deflate(ncid,varid,1,2,2);
			return 0;
		}
	")
        file(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/netcdf_c_check.c"
                "${netcdf_c_code}")
        try_compile(
                NC_DEFLATE_FOUND "${CMAKE_CURRENT_BINARY_DIR}"
                "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/netcdf_c_check.c"
                CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${NETCDF_INCLUDE_DIRS}"
                LINK_LIBRARIES "${NETCDF_LIBRARIES}"
                LINK_LIBRARIES "${NETCDF_AdditionalLibs}"
                OUTPUT_VARIABLE LOG1)

        if (NC_DEFLATE_FOUND)
            set(NETCDF_LINKER_FLAG "${NETCDF_LIBRARIES}")
        else (NC_DEFLATE_FOUND)
            message(SEND_ERROR "The netCDF library is not functional.")
        endif (NC_DEFLATE_FOUND)
    endif (NOT NETCDF_FOUND)
endif (WIN32)
# ##############################################################################

# ##############################################################################
# Qt
# ##############################################################################
find_package(Qt5 REQUIRED Core Network Widgets Charts Positioning Gui Quick QuickWidgets)
# ##############################################################################

# ##############################################################################
# HMDF
# ##############################################################################
set(HMDF_BUILD_SHARED OFF CACHE BOOL "Build HMDF as shared libraries")
set(HMDF_DISABLE_PYTHON ON CACHE BOOL "Disable Python HMDF libraries")
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/hmdf)
# ##############################################################################

# ##############################################################################
# LIBTIDE
# ##############################################################################
add_library(tide STATIC
        ${CMAKE_SOURCE_DIR}/thirdparty/libtcd-2.2.7/bit_pack.c
        ${CMAKE_SOURCE_DIR}/thirdparty/libtcd-2.2.7/tide_db.c
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Amplitude.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Angle.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Banner.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Calendar.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/CalendarFormC.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/CalendarFormH.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/CalendarFormL.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/CalendarFormNotC.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/CalendarFormT.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/ClientSideFont.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Colors.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Constituent.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/ConstituentSet.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Coordinates.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/CurrentBearing.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Date.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/DStr.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Global.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Graph.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/HarmonicsFile.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/HarmonicsPath.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Interval.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/MetaField.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Nullable.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/NullableInterval.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/NullablePredictionValue.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Offsets.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/PixelatedGraph.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/PredictionValue.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/RGBGraph.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Settings.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Skycal.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Speed.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Station.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/StationIndex.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/StationRef.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/SubordinateStation.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/SVGGraph.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/TTYGraph.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/TideEvent.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/TideEventsOrganizer.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Timestamp.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Units.cc
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide/Year.cc
        )

target_include_directories(tide PRIVATE ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide)
target_include_directories(tide PRIVATE ${CMAKE_SOURCE_DIR}/thirdparty/libtcd-2.2.7/UNX)
target_compile_definitions(tide PRIVATE HAVE_DIRENT_H HAVE_DLFCN_H HAVE_GOOD_STRFTIME HAVE_INTTYPES_H HAVE_LANGINFO_H HAVE_LIBTCD
        HAVE_LLROUND HAVE_MEMORY_H HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRINGS_H HAVE_STRING_H HAVE_SYSLOG_H HAVE_SYS_RESOURCE_H
        HAVE_SYS_STAT_H HAVE_SYS_TYPES_H HAVE_UNISTD_H STDC_HEADERS X_DISPLAY_MISSING acceptarg3_t=socklen_t xttpd_group="nobody"
        xttpd_user="nobody" VERSION="2.15.1" PACKAGE_VERSION="2.15.1" PACKAGE_NAME="XTide"
        PACKAGE_BUGREPORT="dave@flaterco.com" PACKAGE="xtide")
# ##############################################################################

# ##############################################################################
# LIBMETOCEAN
# ##############################################################################
IF(BUILD_SHARED_LIBMETOCEAN)
    SET(LIBMETOCEAN_TYPE SHARED)
ELSE()
    SET(LIBMETOCEAN_TYPE STATIC)
ENDIF()
add_library(metocean ${LIBMETOCEAN_TYPE}
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/crmsdata.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/metocean_init.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/movStation.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/noaacoops.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/stringutil.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/waterdata.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/usgswaterdata.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/xtidedata.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/tideprediction.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/ndbcdata.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/stationlocations.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/generic.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/constants.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/highwatermarks.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/hwmdata.cpp
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/tide.qrc
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean/resource_files.qrc)

target_include_directories(metocean PRIVATE ${CMAKE_SOURCE_DIR}/thirdparty/hmdf/libhmdf/include
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide ${NETCDF_INCLUDE_DIR})
target_link_libraries(metocean hmdf tide ${NETCDF_LIBRARIES} Qt5::Core Qt5::Widgets Qt5::Network Qt5::Gui Qt5::Positioning)
add_dependencies(metocean hmdf tide)
set_target_properties(metocean PROPERTIES AUTORCC TRUE)
set_target_properties(metocean PROPERTIES AUTOMOC TRUE)
set_target_properties(metocean PROPERTIES MACOSX_RPATH OFF)
# ##############################################################################

# ##############################################################################
# MetOceanViewer
# ##############################################################################
add_executable(MetOceanViewer
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/chartoptions.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/chartoptionsmenu.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/combobox.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/datebox.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/doublespinbox.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/fileinfo.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/main.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/mainwindow.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/chartview.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/mapchartwidget.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/mapview.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/metocean.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/ndbctab.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/newtabdialog.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/noaaproductlist.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/noaatab.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/stationlist.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/stationmodel.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/mapfunctions.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/tabbar.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/tabwidget.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/unitsmenu.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/userdataform.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/userdatamap.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/userdataseries.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/userdatatab.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/userdatatable.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/usgstab.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/xtidetab.cpp
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/resources.qrc
        ${CMAKE_SOURCE_DIR}/MetOceanViewer2/mainwindow.ui
        )
target_include_directories(MetOceanViewer PRIVATE metocean
        ${CMAKE_SOURCE_DIR}/libraries/libmetocean
        ${CMAKE_SOURCE_DIR}/thirdparty/hmdf/libhmdf/include
        ${CMAKE_SOURCE_DIR}/thirdparty/xtide-2.15.1/libxtide ${NETCDF_INCLUDE_DIR})
target_link_libraries(MetOceanViewer metocean hmdf tide ${NETCDF_LIBRARIES} Qt5::Core
        Qt5::Charts Qt5::Widgets Qt5::Network Qt5::Gui Qt5::Positioning Qt5::Quick Qt5::QuickWidgets)
add_dependencies(MetOceanViewer metocean hmdf tide)
set_target_properties(MetOceanViewer PROPERTIES AUTORCC TRUE)
set_target_properties(MetOceanViewer PROPERTIES AUTOMOC TRUE)
set_target_properties(MetOceanViewer PROPERTIES AUTOUIC TRUE)
set_target_properties(MetOceanViewer PROPERTIES MACOSX_RPATH OFF)
# ##############################################################################
