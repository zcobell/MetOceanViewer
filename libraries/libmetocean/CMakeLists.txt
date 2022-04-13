
file(GLOB LIBMETOCEAN_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc")
add_library( metocean STATIC ${LIBMETOCEAN_sources})

add_dependencies(metocean tide)

set(TIDE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide)

target_include_directories(metocean PRIVATE ${NETCDF_INCLUDE} ${BOOST_INCLUDE} ${TIDE_INCLUDE})
target_link_libraries(metocean tide Qt6::Core Qt6::Positioning Qt6::Network Qt6::Gui ${NETCDF_LIB})
