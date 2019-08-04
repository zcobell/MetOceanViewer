

cmake_minimum_required(VERSION 3.0)
SET(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMakeModules)

###########################################################################
# C++ 11 Check
###########################################################################
INCLUDE(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
IF(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
ELSE(COMPILER_SUPPORTS_CXX11)
    message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
ENDIF(COMPILER_SUPPORTS_CXX11)
###########################################################################

###########################################################################
#  NETCDF
###########################################################################
IF(WIN32)
    SET(NETCDF_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/thirdparty/netcdf/include" ) 
ELSE(WIN32)
	IF(NOT "${NETCDFHOME}" STREQUAL "")
		SET(NETCDF_DIR "${NETCDFHOME}")
	ELSEIF(NOT $ENV{NETCDFHOME} STREQUAL "")
		SET(NETCDF_DIR $ENV{NETCDFHOME})
	ENDIF(NOT "${NETCDFHOME}" STREQUAL "")

	FIND_PACKAGE(NetCDF)

	IF(NOT NETCDF_FOUND)
		MESSAGE(SEND_ERROR "Specify the netCDF path on the following screen")
	ELSE(NOT NETCDF_FOUND)
		
		SET(netcdf_c_code
	"
		#include <netcdf.h>
		int main(){
			int ncid,varid;
			int ierr = nc_def_var_deflate(ncid,varid,1,2,2);
			return 0;
		}
	"
		)
		FILE(WRITE "${CMAKE_BINARY_DIR}/CMakeFiles/netcdf_c_check.c" "${netcdf_c_code}")
		TRY_COMPILE(NC_DEFLATE_FOUND "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/netcdf_c_check.c" CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${NETCDF_INCLUDE_DIRS}" LINK_LIBRARIES "${NETCDF_LIBRARIES}" LINK_LIBRARIES "${NETCDF_AdditionalLibs}" OUTPUT_VARIABLE LOG1)
			
		IF(NC_DEFLATE_FOUND)
			SET(NETCDF_LINKER_FLAG "${NETCDF_LIBRARIES}")
		ELSE(NC_DEFLATE_FOUND)
			MESSAGE(SEND_ERROR "The netCDF library is not functional.")
		ENDIF(NC_DEFLATE_FOUND)
	ENDIF(NOT NETCDF_FOUND)
ENDIF(WIN32)

add_executable( processCrmsData src/cdate.cpp src/crmsdatacontainer.cpp src/crmsdatabase.cpp src/main.cpp )
target_include_directories( processCrmsData PRIVATE ${CMAKE_SOURCE_DIR}/../thirdparty/boost_1_67_0 ${NETCDF_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src ) 
target_link_libraries( processCrmsData ${NETCDF_LIBRARIES} )
