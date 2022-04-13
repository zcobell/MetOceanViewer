
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

file(GLOB METOCEANVIEWER_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc")
add_executable( MetOceanViewer ${METOCEANVIEWER_sources})

set(EZPROJ_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/ezproj/src)
set(VERSION_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_dependencies(MetOceanViewer metocean tide ezproj)
target_compile_definitions(MetOceanViewer PRIVATE GIT_VERSION="0.0" MOV_COMPILER="mingw" MOV_ARCH="x86_64")

target_include_directories(MetOceanViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../libraries/libmetocean
    ${NETCDF_INCLUDE} ${BOOST_INCLUDE} ${TIDE_INCLUDE}
    ${EZPROJ_INCLUDE} ${VERSION_INCLUDE})

target_link_directories(MetOceanViewer PRIVATE ${NETCDF_LIB_DIRECTORY})

target_link_libraries(MetOceanViewer metocean tide ezproj
    Qt6::Core Qt6::Positioning Qt6::Network Qt6::Gui
    Qt6::Charts Qt6::PrintSupport Qt6::QuickWidgets ${NETCDF_LIBRARY})
