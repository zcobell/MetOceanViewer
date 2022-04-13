
project(MetOcean)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core Gui Network Positioning Charts PrintSupport Quick QuickWidgets REQUIRED)

set(BOOST_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/boost_1_78_0)

if(WIN32)
    set(NETCDF_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/netcdf)
    set(NETCDF_LIB_DIRECTORY ${NETCDF_DIRECTORY}/libs_vc64)
    set(NETCDF_INCLUDE ${NETCDF_DIRECTORY}/include)
    set(NETCDF_LIBRARY -lnetcdf -lhdf5 -lzlib -llibcurl_imp)
elseif(UNIX)

endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ezproj)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libraries/libtide)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/libraries/libmetocean)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/MetOceanViewer)
