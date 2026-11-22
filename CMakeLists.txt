# SPDX-License-Identifier: MIT
#
# Layout:
#   framework/   reusable engine -- widget tree, WAML parser, MVVM, renderers,
#                resource loading. Knows nothing about any particular app.
#   samples/     consumers of the framework. Each sample owns its ViewModel,
#                markup and .rc; it links the framework, never the reverse.
#   tests/       framework test suites.
#   bench/       framework benchmarks.
#   tools/       framework harnesses (snapshot / interact / probe / dpishot).
#
# NOTE: build.ps1 is the primary build path in this environment; CMake is kept
# in sync for a normal developer machine. See README.md for why.
cmake_minimum_required(VERSION 3.25)
project(WamlSpike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Framework library.
# ---------------------------------------------------------------------------
set(FRAMEWORK_SRC
    framework/src/core/Core.cpp
    framework/src/core/Widget.cpp
    framework/src/core/Types.cpp
    framework/src/core/Controls.cpp
    framework/src/mvvm/Binding.cpp
    framework/src/waml/Diagnostics.cpp
    framework/src/waml/Parser.cpp
    framework/src/render/SoftwareRender.cpp
    framework/src/platform/Resources.cpp
)

add_library(wui_framework STATIC ${FRAMEWORK_SRC})
target_include_directories(wui_framework PUBLIC
    framework/src
    framework/src/core
    framework/src/mvvm
    framework/src/waml
    framework/src/render
    framework/src/platform
)
target_link_libraries(wui_framework PUBLIC d2d1 dwrite windowscodecs ole32)
target_compile_definitions(wui_framework PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

# Optional D2D backend. Separate so headless tools need no GPU stack.
add_library(wui_render_d2d STATIC framework/src/render/D2DRender.cpp)
target_link_libraries(wui_render_d2d PUBLIC wui_framework d2d1 dwrite windowscodecs ole32)

# ---------------------------------------------------------------------------
# Sample: gallery. Owns its ViewModel, markup and resources.
# ---------------------------------------------------------------------------
set(GALLERY_DIR samples/gallery)

set(GALLERY_RES_SRC ${CMAKE_CURRENT_BINARY_DIR}/gallery/app.res)
add_custom_command(
    OUTPUT ${GALLERY_RES_SRC}
    COMMAND rc /nologo /fo${GALLERY_RES_SRC}
            /dWUI_VERSION_MAJOR=0 /dWUI_VERSION_MINOR=5 /dWUI_VERSION_PATCH=0 /dWUI_VERSION_BUILD=0
            ${CMAKE_CURRENT_SOURCE_DIR}/${GALLERY_DIR}/resources/app.rc
    DEPENDS ${GALLERY_DIR}/resources/app.rc ${GALLERY_DIR}/Gallery.waml
    COMMENT "Compiling gallery resources"
)

add_executable(gallery WIN32 ${GALLERY_DIR}/App.cpp ${GALLERY_RES_SRC})
target_include_directories(gallery PRIVATE ${GALLERY_DIR} ${GALLERY_DIR}/resources)
target_link_libraries(gallery PRIVATE wui_render_d2d)

# ---------------------------------------------------------------------------
# Framework harnesses.
# ---------------------------------------------------------------------------
add_executable(waml_tests tests/Tests.cpp)
target_link_libraries(waml_tests PRIVATE wui_framework)

add_executable(waml_bench bench/Bench.cpp)
target_link_libraries(waml_bench PRIVATE wui_framework)

add_executable(waml_snapshot tools/Snapshot.cpp)
target_include_directories(waml_snapshot PRIVATE ${GALLERY_DIR} ${GALLERY_DIR}/resources)
target_link_libraries(waml_snapshot PRIVATE wui_render_d2d)

add_executable(waml_interact tools/Interact.cpp)
target_include_directories(waml_interact PRIVATE ${GALLERY_DIR} ${GALLERY_DIR}/resources)
target_link_libraries(waml_interact PRIVATE wui_framework)

add_executable(waml_dpishot tools/DpiShot.cpp)
target_include_directories(waml_dpishot PRIVATE ${GALLERY_DIR} ${GALLERY_DIR}/resources)
target_link_libraries(waml_dpishot PRIVATE wui_render_d2d)

enable_testing()
add_test(NAME unit COMMAND waml_tests)
