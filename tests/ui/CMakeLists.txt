# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Both executables share main.cpp (the app's startup steps, then Catch2).

# Label qt: identity, version and the maplibre plugin, without GL.
mov_add_test(mov_ui_qt_tests QT SOURCES main.cpp test_application.cpp LIBRARIES mov::ui mov::core Qt6::Location)
mov_stage_maplibre_runtime(mov_ui_qt_tests)
mov_stage_projection_data(mov_ui_qt_tests)

# The application's own --self-test, as the packaging smoke test runs it
# (docs/packaging.md), from the build tree.
add_test(NAME metoceanviewer_self_test COMMAND metoceanviewer --self-test)
set_tests_properties(
    metoceanviewer_self_test
    PROPERTIES LABELS qt TIMEOUT 120 ENVIRONMENT QT_QPA_PLATFORM=offscreen
)
if(WIN32)
    set_property(
        TEST metoceanviewer_self_test
        APPEND
        PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

# Label gui: rendering, and the screenshot for humans.
mov_add_test(
    mov_ui_tests
    GUI
    SOURCES main.cpp test_main_window.cpp
    LIBRARIES mov::ui Qt6::Quick Qt6::Location Qt6::Test
)
mov_stage_maplibre_runtime(mov_ui_tests)
target_compile_definitions(mov_ui_tests PRIVATE "MOV_SCREENSHOT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/screenshots\"")

# QML formatting (qmlformat), and the check rejecting what it exists to reject.
set(check_qml_format "${PROJECT_SOURCE_DIR}/cmake/CheckQmlFormat.cmake")
add_test(
    NAME qml_format
    COMMAND
        ${CMAKE_COMMAND} "-DQMLFORMAT=$<TARGET_FILE:Qt6::qmlformat>" "-DMOV_QML_DIRS=${PROJECT_SOURCE_DIR}/src/ui" -P
        "${check_qml_format}"
)
add_test(
    NAME qml_format_rejects_unformatted
    COMMAND
        ${CMAKE_COMMAND} "-DQMLFORMAT=$<TARGET_FILE:Qt6::qmlformat>"
        "-DMOV_QML_DIRS=${PROJECT_SOURCE_DIR}/tests/cmake/qmlformat" -P "${check_qml_format}"
)
set_tests_properties(qml_format qml_format_rejects_unformatted PROPERTIES LABELS qt)
set_tests_properties(qml_format_rejects_unformatted PROPERTIES PASS_REGULAR_EXPRESSION "Not qmlformat-formatted")
