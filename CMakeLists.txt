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
    framework/src/core/Chart.cpp
    framework/src/core/ChartData.cpp
    framework/src/core/Overlay.cpp
    framework/src/core/Animation.cpp
    framework/src/core/VisualStates.cpp
    framework/src/core/TextInput.cpp
    framework/src/mvvm/Binding.cpp
    framework/src/mvvm/Converters.cpp
    framework/src/waml/Diagnostics.cpp
    framework/src/waml/Parser.cpp
    framework/src/waml/StyleTriggers.cpp
    framework/src/render/SoftwareRender.cpp
    framework/src/platform/Resources.cpp
    framework/src/platform/FileDialogWin32.cpp
    framework/src/platform/Image.cpp
    framework/src/platform/ImageWin32.cpp
    framework/src/platform/TextInputWin32.cpp
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
target_link_libraries(wui_framework PUBLIC d2d1 dwrite windowscodecs ole32 version)
target_compile_definitions(wui_framework PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

# Optional D2D backend. Separate so headless tools need no GPU stack.
add_library(wui_render_d2d STATIC framework/src/render/D2DRender.cpp)
target_link_libraries(wui_render_d2d PUBLIC wui_framework d2d1 dwrite windowscodecs ole32)


# ---------------------------------------------------------------------------
# Samples. Each owns its ViewModel, markup and resources; the Win32/D2D plumbing
# lives in samples/shared and is compiled into every sample.
# ---------------------------------------------------------------------------
set(SAMPLE_SHARED_DIR samples/shared)
set(SAMPLE_INCLUDES
    ${SAMPLE_SHARED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/basic
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/basic/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/list
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/list/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/binding
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/binding/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/theming
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/theming/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/usercontrol
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/usercontrol/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/editor
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/editor/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/charts
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/charts/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/animation
    ${CMAKE_CURRENT_SOURCE_DIR}/samples/animation/resources)

# One function so adding a sample stays a single call.
function(add_sample NAME)
    set(dir ${CMAKE_CURRENT_SOURCE_DIR}/samples/${NAME})
    set(res ${CMAKE_CURRENT_BINARY_DIR}/${NAME}/app.res)
    add_custom_command(
        OUTPUT ${res}
        COMMAND rc /nologo /fo${res}
                /dWUI_VERSION_MAJOR=0 /dWUI_VERSION_MINOR=14
                /dWUI_VERSION_PATCH=1 /dWUI_VERSION_BUILD=0
                ${dir}/resources/app.rc
        DEPENDS ${dir}/resources/app.rc
        COMMENT "Compiling ${NAME} resources"
    )
    # Every .cpp beside App.cpp belongs to the sample: a ViewModel with real
    # logic (the editor's close flow) lives in its own translation unit.
    file(GLOB SAMPLE_SOURCES CONFIGURE_DEPENDS ${dir}/*.cpp)
    add_executable(waml_${NAME} WIN32
        ${SAMPLE_SOURCES}
        ${SAMPLE_SHARED_DIR}/SampleHost.cpp
        ${res})
    target_include_directories(waml_${NAME} PRIVATE ${dir} ${dir}/resources ${SAMPLE_SHARED_DIR})
    target_link_libraries(waml_${NAME} PRIVATE wui_render_d2d)
endfunction()

add_sample(basic)
add_sample(list)
add_sample(binding)
add_sample(theming)
add_sample(usercontrol)
add_sample(editor)
add_sample(charts)
add_sample(animation)

# ---------------------------------------------------------------------------
# Framework harnesses. These read sample markup and ViewModels, so they carry
# every sample's include path.
# ---------------------------------------------------------------------------
add_executable(waml_tests tests/Tests.cpp)
target_link_libraries(waml_tests PRIVATE wui_framework)

add_executable(waml_bench bench/Bench.cpp)
target_link_libraries(waml_bench PRIVATE wui_framework)

# The harnesses drive the editor's ViewModel, whose logic lives in its own TU.
set(SAMPLE_VM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/samples/editor/EditorVm.cpp)

add_executable(waml_snapshot tools/Snapshot.cpp ${SAMPLE_VM_SOURCES})
target_include_directories(waml_snapshot PRIVATE ${SAMPLE_INCLUDES})
target_link_libraries(waml_snapshot PRIVATE wui_render_d2d)

add_executable(waml_interact tools/Interact.cpp ${SAMPLE_VM_SOURCES})
target_include_directories(waml_interact PRIVATE ${SAMPLE_INCLUDES})
target_link_libraries(waml_interact PRIVATE wui_framework)

add_executable(waml_dpishot tools/DpiShot.cpp)
target_include_directories(waml_dpishot PRIVATE ${SAMPLE_INCLUDES})
target_link_libraries(waml_dpishot PRIVATE wui_render_d2d)

# ---------------------------------------------------------------------------
# basic_py: the ViewModel lives in Python; the UI engine stays in C++.
#
# Two artifacts share the same implementation:
#   _wui.pyd           loaded by `python main.py`
#   waml_basic_py.exe  a launcher that embeds the SYSTEM Python and imports main.py
# Neither ships a Python runtime: both link python3.lib and load python3.dll from
# the interpreter installed on the machine. Py_LIMITED_API keeps the extension
# loadable by any CPython >= 3.8.
# ---------------------------------------------------------------------------
set(PY_SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/samples/basic_py)
set(WUI_PY_OUT "${CMAKE_BINARY_DIR}/basic_py" CACHE PATH
    "Output directory for the Python-driven sample")

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter Development.Module Development.Embed)
string(REPLACE "\\" "\\\\" WUI_PYTHON_EXE_C "${Python3_EXECUTABLE}")

# CMake's Python3::Python / Python3::Module point at pythonXY.lib on Windows.
# The stable ABI import library is python3.lib: it loads python3.dll, which
# forwards to whichever CPython 3.x is installed on the machine.
get_filename_component(WUI_PYTHON_PREFIX "${Python3_EXECUTABLE}" DIRECTORY)
find_library(WUI_PYTHON3_STABLE_LIB NAMES python3
             PATHS "${WUI_PYTHON_PREFIX}/libs" ${Python3_LIBRARY_DIRS} NO_DEFAULT_PATH)
if(NOT WUI_PYTHON3_STABLE_LIB)
    message(FATAL_ERROR "python3.lib (stable ABI import library) not found")
endif()

configure_file(${PY_SAMPLE_DIR}/native/PythonPaths.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/PythonPaths.h @ONLY)

# The D2D render backend is compiled in so the pyd can own a real window; the
# shared host keeps Win32/DPI/input in one place for every sample.
add_library(_wui MODULE
    ${PY_SAMPLE_DIR}/native/PyBridge.cpp
    ${SAMPLE_SHARED_DIR}/SampleHost.cpp)
target_include_directories(_wui PRIVATE
    ${PY_SAMPLE_DIR}/native
    ${CMAKE_CURRENT_BINARY_DIR}
    ${SAMPLE_SHARED_DIR}
    ${Python3_INCLUDE_DIRS})
target_link_libraries(_wui PRIVATE wui_render_d2d ${WUI_PYTHON3_STABLE_LIB})
target_compile_definitions(_wui PRIVATE Py_LIMITED_API=0x03080000 WUI_PY_BRIDGE)
# Windows import machinery only probes ".pyd" (it does not list ".abi3.pyd"
# in EXTENSION_SUFFIXES), even though the module is built against the stable
# ABI. Keep the plain suffix so `import _wui` works without a loader hook.
set_target_properties(_wui PROPERTIES PREFIX "" SUFFIX ".pyd")

# The launcher deliberately uses only stable-ABI entry points (python3.lib does
# not export PyConfig on Windows), so it works with any CPython 3.x on PATH.
add_executable(waml_basic_py WIN32
    ${PY_SAMPLE_DIR}/native/PyLauncher.cpp
    ${PY_SAMPLE_DIR}/resources/app.rc)
target_include_directories(waml_basic_py PRIVATE
    ${PY_SAMPLE_DIR}/native
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Python3_INCLUDE_DIRS})
target_link_libraries(waml_basic_py PRIVATE ${WUI_PYTHON3_STABLE_LIB})
add_dependencies(waml_basic_py _wui)
# PerMonitorV2 must come from the manifest; the host also sets the DPI context
# at runtime, but the manifest is what keeps the process from being virtualised.
target_link_options(waml_basic_py PRIVATE
    "/MANIFESTINPUT:${PY_SAMPLE_DIR}/resources/app.manifest")

# Force both artifacts into one flat directory so the exe always finds the
# extension and the three loose payload files next to itself.
function(wui_py_single_output_dir target)
    foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${cfg} "${WUI_PY_OUT}"
            LIBRARY_OUTPUT_DIRECTORY_${cfg} "${WUI_PY_OUT}"
            ARCHIVE_OUTPUT_DIRECTORY_${cfg} "${WUI_PY_OUT}")
    endforeach()
endfunction()
wui_py_single_output_dir(_wui)
wui_py_single_output_dir(waml_basic_py)

add_custom_command(TARGET waml_basic_py POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PY_SAMPLE_DIR}/main.py ${PY_SAMPLE_DIR}/wui.py
            ${PY_SAMPLE_DIR}/BasicPy.waml $<TARGET_FILE_DIR:waml_basic_py>
    COMMENT "Copying Python sample payload next to waml_basic_py.exe")
add_custom_command(TARGET _wui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:_wui> ${PY_SAMPLE_DIR}
    COMMENT "Copying _wui extension next to main.py")

enable_testing()
add_test(NAME unit COMMAND waml_tests)
