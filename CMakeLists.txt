cmake_minimum_required(VERSION 3.20)
project(ColorizingDMDComfyUI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The editor API (ColorizingDMD's plugin-api, version 3) and the VPX plugin SDK it builds on
# (vpinball's plugins/plugins), as the editor is built with.
set(COLORIZINGDMD_DIR "" CACHE PATH "ColorizingDMD sources")
set(VPINBALL_DIR "" CACHE PATH "vpinball sources, for the plugin SDK")
# Where a development editor looks for plugins (its build's vpx-plugins folder): the plugin is
# copied there after each build when set.
set(COLORIZINGDMD_PLUGIN_DIR "" CACHE PATH "Editor plugin folder to copy the plugin into")

if(NOT EXISTS "${COLORIZINGDMD_DIR}/plugin-api/ColorizingDMDPlugin.h")
    message(FATAL_ERROR "Set COLORIZINGDMD_DIR to the ColorizingDMD sources.")
endif()
if(NOT EXISTS "${VPINBALL_DIR}/plugins/plugins/MsgPlugin.h")
    message(FATAL_ERROR "Set VPINBALL_DIR to the vpinball sources (plugins/plugins).")
endif()

find_package(Threads REQUIRED)

# Everything but the entry points, for the plugin and the tests.
add_library(comfyui_core STATIC
    src/ComfyClient.cpp
    src/ComfyClient.h
    src/Gather.cpp
    src/Gather.h
    src/Images.cpp
    src/Images.h
    src/Pictures.cpp
    src/Pictures.h
    src/Run.cpp
    src/Run.h
    src/Workflow.cpp
    src/Workflow.h
)
target_include_directories(comfyui_core PUBLIC src)
target_include_directories(comfyui_core SYSTEM PUBLIC
    "${COLORIZINGDMD_DIR}/plugin-api"
    "${VPINBALL_DIR}/plugins"
    "${PROJECT_SOURCE_DIR}/third-party"
    "${PROJECT_SOURCE_DIR}/third-party/httplib"
)
target_link_libraries(comfyui_core PUBLIC Threads::Threads)
set_target_properties(comfyui_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_link_libraries(comfyui_core PUBLIC ws2_32)
    # Windows 10 and later (httplib needs Vista or later).
    target_compile_definitions(comfyui_core PUBLIC _WIN32_WINNT=0x0A00)
endif()

add_library(ComfyUIPlugin MODULE src/Plugin.cpp)
target_link_libraries(ComfyUIPlugin PRIVATE comfyui_core)

foreach(target comfyui_core ComfyUIPlugin)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# The file names plugin.cfg gives.
set_target_properties(ComfyUIPlugin PROPERTIES PREFIX "" OUTPUT_NAME plugin-comfyui)
if(WIN32)
    set_target_properties(ComfyUIPlugin PROPERTIES OUTPUT_NAME plugin-comfyui64)
elseif(APPLE)
    set_target_properties(ComfyUIPlugin PROPERTIES SUFFIX ".dylib")
endif()

# The plugin's folder, as the editor's plugin folder holds it: ComfyUI/plugin.cfg and the
# library.
set(plugin_folder "${CMAKE_BINARY_DIR}/ComfyUI")
add_custom_command(TARGET ComfyUIPlugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${plugin_folder}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/plugin.cfg" "${plugin_folder}/"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:ComfyUIPlugin>" "${plugin_folder}/"
    VERBATIM
)
if(COLORIZINGDMD_PLUGIN_DIR)
    add_custom_command(TARGET ComfyUIPlugin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${plugin_folder}"
                "${COLORIZINGDMD_PLUGIN_DIR}/ComfyUI"
        VERBATIM
    )
endif()

# Tests: the pictures, the workflow and the gathering on a fake editor, and the client and the
# run against a stub of ComfyUI's API in the test program; the custom node's own tests when
# Python 3 is found (the parts that need ComfyUI's Python packages are skipped without them).
include(CTest)
if(BUILD_TESTING)
    add_executable(comfyui_tests tests/ComfyUITests.cpp)
    target_link_libraries(comfyui_tests PRIVATE comfyui_core)
    target_compile_definitions(comfyui_tests PRIVATE
        COMFYUI_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
    if(NOT MSVC)
        target_compile_options(comfyui_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME comfyui_tests COMMAND comfyui_tests)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME comfyui_node_tests
                 COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s
                         "${PROJECT_SOURCE_DIR}/custom_nodes" -p "test_*.py" -v)
    endif()
endif()
