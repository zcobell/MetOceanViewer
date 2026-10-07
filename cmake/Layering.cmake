# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# The layer order of docs/rearchitecture-plan.md §2.1, declared once, and the
# configure-time check that enforces it.
#
#   core <- io <- providers <- app <- ui      (a layer links only layers below it)
#   cli  -> providers, io, core               (never app or ui)
#   core and io never link Qt
#
# mov_add_module(<layer> SOURCES ... [PUBLIC_LINK ...] [PRIVATE_LINK ...])
#   creates the static library mov_<layer> (alias mov::<layer>) from
#   src/<layer>, with public headers in src/<layer>/include, and registers it
#   for the check. Executables (the cli, the app binary) call
#   mov_enforce_layer(<target> <layer>) themselves.
#
# mov_check_layering(), called once at the end of the top-level CMakeLists.txt,
# walks every registered target's link graph and fails the configure on a
# violation. What it inspects, per target, transitively:
#   LINK_LIBRARIES, INTERFACE_LINK_LIBRARIES, INTERFACE_LINK_LIBRARIES_DIRECT,
#   LINK_OPTIONS, INTERFACE_LINK_OPTIONS, INCLUDE_DIRECTORIES and
#   INTERFACE_INCLUDE_DIRECTORIES.
# Every name-like token in those values counts, including ones inside generator
# expressions ($<LINK_ONLY:Qt6::Core>, $<TARGET_PROPERTY:Qt6::Core,...>) and
# flags (-lQt6Core). This over-approximates on purpose: a conditional Qt link
# is still a Qt link. It cannot see link flags added through
# CMAKE_*_LINKER_FLAGS or toolchain files, and the source-level counterpart is
# the qt_free_sources test (cmake/CheckQtFreeSources.cmake).

# Lowest first.
set(MOV_LAYERS
    core
    io
    providers
    app
    ui
)
set(MOV_QT_FREE_LAYERS core io)
# Layers outside the stack, with the layers each may link.
set(MOV_SIDE_LAYERS cli)
set(MOV_LAYER_cli_MAY_LINK core io providers)

# A Qt target (Qt6::Core, versionless Qt::Core), a Qt library named as a flag
# (-lQt6Core), a file (libQt6Core.so, Qt6Core.lib) or a bare name (Qt6Core),
# a framework (QtCore.framework) or a Qt include directory (.../include/QtCore).
set(_mov_qt_token_regex "^(Qt[0-9]*::|-lQt[0-9]|(lib)?Qt[0-9]+[A-Z]|Qt[A-Z][A-Za-z0-9]*($|\\.framework))")

function(_mov_layer_may_link layer out_var)
    if(layer IN_LIST MOV_SIDE_LAYERS)
        set(${out_var} ${MOV_LAYER_${layer}_MAY_LINK} PARENT_SCOPE)
        return()
    endif()
    list(FIND MOV_LAYERS "${layer}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "Unknown layer '${layer}'; known: ${MOV_LAYERS} ${MOV_SIDE_LAYERS}")
    endif()
    if(index EQUAL 0)
        set(${out_var} "" PARENT_SCOPE)
    else()
        list(SUBLIST MOV_LAYERS 0 ${index} below)
        set(${out_var} ${below} PARENT_SCOPE)
    endif()
endfunction()

# Record that <target> belongs to <layer>: it may link only the layers below
# it, and no Qt if the layer is Qt-free.
function(mov_enforce_layer target layer)
    _mov_layer_may_link(${layer} allowed)
    set(forbidden ${MOV_LAYERS} ${MOV_SIDE_LAYERS})
    list(REMOVE_ITEM forbidden ${layer} ${allowed})
    list(JOIN forbidden "|" forbidden_alternatives)
    set(patterns "^mov(_|::)(${forbidden_alternatives})$")
    if(layer IN_LIST MOV_QT_FREE_LAYERS)
        list(APPEND patterns "${_mov_qt_token_regex}")
    endif()
    set_property(GLOBAL APPEND PROPERTY MOV_LAYERED_TARGETS ${target})
    set_property(GLOBAL PROPERTY MOV_LAYER_PATTERNS_${target} ${patterns})
endfunction()

function(mov_add_module layer)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;PUBLIC_LINK;PRIVATE_LINK")
    _mov_layer_may_link(${layer} unused) # validates the layer name
    set(target mov_${layer})
    add_library(${target} STATIC)
    add_library(mov::${layer} ALIAS ${target})
    target_sources(${target} PRIVATE ${arg_SOURCES})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_link_libraries(${target} PUBLIC ${arg_PUBLIC_LINK} PRIVATE ${arg_PRIVATE_LINK} mov::options mov::warnings)
    mov_enable_static_analysis(${target})
    mov_enforce_layer(${target} ${layer})
endfunction()

# Source directories of the Qt-free layers that exist in this build.
function(mov_qt_free_source_dirs out_var)
    set(dirs "")
    foreach(layer IN LISTS MOV_QT_FREE_LAYERS)
        if(TARGET mov_${layer})
            list(APPEND dirs "${PROJECT_SOURCE_DIR}/src/${layer}")
        endif()
    endforeach()
    set(${out_var} "${dirs}" PARENT_SCOPE)
endfunction()

# Name-like tokens of a target's link and include properties. The project's own
# source and build paths are stripped first so the checkout location (say,
# ~/QtProjects/...) cannot look like a Qt directory.
function(_mov_link_tokens target out_var)
    set(tokens "")
    foreach(
        property
        LINK_LIBRARIES
        INTERFACE_LINK_LIBRARIES
        INTERFACE_LINK_LIBRARIES_DIRECT
        LINK_OPTIONS
        INTERFACE_LINK_OPTIONS
        INCLUDE_DIRECTORIES
        INTERFACE_INCLUDE_DIRECTORIES
    )
        get_target_property(values ${target} ${property})
        if(NOT values)
            continue()
        endif()
        foreach(value IN LISTS values)
            string(REPLACE "${PROJECT_BINARY_DIR}" "" value "${value}")
            string(REPLACE "${PROJECT_SOURCE_DIR}" "" value "${value}")
            string(REGEX MATCHALL "[A-Za-z0-9_.+-]+(::[A-Za-z0-9_.+-]+)*" found "${value}")
            list(APPEND tokens ${found})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES tokens)
    set(${out_var} "${tokens}" PARENT_SCOPE)
endfunction()

# Breadth-first walk of root's link graph. Each node remembers its path from
# root so the report names the offending chain; a branch stops at its first
# forbidden token.
function(_mov_find_violations root patterns out_var)
    set(found "")
    set(queue "${root}")
    set(visited "")
    string(MAKE_C_IDENTIFIER "${root}" key)
    set(path_${key} "${root}")
    while(queue)
        list(POP_FRONT queue node)
        if(node IN_LIST visited)
            continue()
        endif()
        list(APPEND visited "${node}")
        string(MAKE_C_IDENTIFIER "${node}" node_key)
        if(NOT node STREQUAL root)
            set(hit "")
            foreach(pattern IN LISTS patterns)
                if(node MATCHES "${pattern}")
                    set(hit "${pattern}")
                    break()
                endif()
            endforeach()
            if(hit)
                string(REPLACE ";" " -> " chain "${path_${node_key}}")
                list(APPEND found "  ${chain}  (matches ${hit})")
                continue()
            endif()
        endif()
        if(NOT TARGET "${node}")
            continue()
        endif()
        get_target_property(aliased "${node}" ALIASED_TARGET)
        if(aliased)
            set(children "${aliased}")
        else()
            _mov_link_tokens("${node}" children)
        endif()
        foreach(child IN LISTS children)
            string(MAKE_C_IDENTIFIER "${child}" child_key)
            if(NOT DEFINED path_${child_key})
                set(path_${child_key} ${path_${node_key}} "${child}")
            endif()
            list(APPEND queue "${child}")
        endforeach()
    endwhile()
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

function(mov_check_layering)
    get_property(layered GLOBAL PROPERTY MOV_LAYERED_TARGETS)
    set(violations "")
    foreach(target IN LISTS layered)
        get_property(patterns GLOBAL PROPERTY MOV_LAYER_PATTERNS_${target})
        _mov_find_violations("${target}" "${patterns}" found)
        list(APPEND violations ${found})
    endforeach()
    if(violations)
        list(JOIN violations "\n" report)
        message(FATAL_ERROR "Layering violation (see docs/rearchitecture-plan.md §2.1):\n${report}")
    endif()
endfunction()
