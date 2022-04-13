
set(LIBTIDE_sources ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libtcd-2.2.7/bit_pack.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libtcd-2.2.7/tide_db.c
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Amplitude.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Angle.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Banner.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Calendar.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormC.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormH.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormL.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormNotC.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/CalendarFormT.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/ClientSideFont.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Colors.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Constituent.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/ConstituentSet.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Coordinates.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/CurrentBearing.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Date.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/DStr.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Global.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Graph.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/HarmonicsFile.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/HarmonicsPath.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Interval.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/MetaField.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Nullable.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/NullableInterval.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/NullablePredictionValue.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Offsets.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/PixelatedGraph.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/PredictionValue.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/RGBGraph.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Settings.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Skycal.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Speed.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Station.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/StationIndex.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/StationRef.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/SubordinateStation.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/SVGGraph.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/TTYGraph.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/TideEvent.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/TideEventsOrganizer.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Timestamp.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Units.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/xtide-2.15.1/libxtide/Year.cc)

add_library(tide STATIC ${LIBTIDE_sources})

if(WIN32)
    target_include_directories(tide PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libtcd-2.2.7/DOS)
    target_compile_definitions(tide PRIVATE HAVE_INTTYPES_H HAVE_IO_H HAVE_LIBTCD HAVE_LLROUND HAVE_MEMORY_H HAVE_PROCESS_H
               HAVE_LIBTCD HAVE_LLROUND HAVE_MEMORY_H HAVE_PROCESS_H USE_PRAGMA_MESSAGE
               HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRING_H HAVE_SYS_STAT_H HAVE_SYS_TYPES_H
               STDC_HEADERS USE_PRAGMA_MESSAGE UseLocalFiles UseSemicolonPathsep X_DISPLAY_MISSING
               acceptarg3_t=size_t xttpd_group=\"nobody\" xttpd_user=\"nobody\" VERSION=\"2.15.1\"
               PACKAGE_VERSION=\"2.15.1\" PACKAGE_NAME=\"XTide\" PACKAGE_BUGREPORT=\"dave@flaterco.com\"
               PACKAGE=\"xtide\" ALLOW_BROKEN_ZONEINFO )
elseif(UNIX)
    target_include_directories(tide PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libtcd-2.2.7/UNX)
    target_compile_definitions(tide PRIVATE HAVE_DIRENT_H HAVE_DLFCN_H HAVE_GOOD_STRFTIME HAVE_INTTYPES_H HAVE_LANGINFO_H HAVE_LIBTCD
             HAVE_LLROUND HAVE_MEMORY_H HAVE_STDINT_H HAVE_STDLIB_H HAVE_STRINGS_H HAVE_STRING_H HAVE_SYSLOG_H HAVE_SYS_RESOURCE_H
             HAVE_SYS_STAT_H HAVE_SYS_TYPES_H HAVE_UNISTD_H STDC_HEADERS X_DISPLAY_MISSING acceptarg3_t=socklen_t xttpd_group=\"nobody\"
             xttpd_user=\"nobody\" VERSION=\"2.15.1\" PACKAGE_VERSION=\"2.15.1\" PACKAGE_NAME=\"XTide\"
             PACKAGE_BUGREPORT=\"dave@flaterco.com\" PACKAGE=\"xtide)
endif()



