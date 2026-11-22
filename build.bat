@echo off
rem SPDX-License-Identifier: MIT
rem
rem WAML/MVVM spike build.
rem
rem Why not CMake/Ninja here: both parse child-process stdout through pipes, and
rem those pipes cannot be created inside the confined sandbox, so configure hangs
rem instead of failing. Driving cl.exe directly keeps stdio inherited.
rem
rem Deliverable modes (this is the part that matters for shipping):
rem   debug    markup read from samples\*.waml at runtime  -> edit and rerun
rem            VERSIONINFO still embedded so the version logic is exercised
rem   release  markup compiled into the exe as RCDATA      -> single file
rem
rem Usage:  build.bat [debug|release|all|...]
setlocal enabledelayedexpansion

set SRC=%~dp0

rem ---------------------------------------------------------------------------
rem Output location.
rem
rem Default is <this dir>\build. Set WUI_OUT to put the binaries somewhere else:
rem
rem   set WUI_OUT=D:\wui-dist
rem   build.bat release
rem
rem Why this exists: executables produced by an automated/sandboxed session can
rem inherit filesystem markers that make Windows Defender SmartScreen show
rem "Windows protected your PC" on every launch, even though the binary is
rem unsigned-but-fine and identical to one that launches silently. Building or
rem copying from an ordinary shell avoids it. Keeping the artifacts outside the
rem agent-created tree is the reliable fix.
rem ---------------------------------------------------------------------------
if "%WUI_OUT%"=="" set OUT=%SRC%build
if not "%WUI_OUT%"=="" set "OUT=%WUI_OUT%"
if not exist "%OUT%" mkdir "%OUT%" 2>nul

set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat
set VSLANG=1033
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed
    exit /b 1
)

rem ---------------------------------------------------------------------------
rem Version stamping: one place feeds VERSIONINFO, the file-properties dialog
rem and the About text. CI overrides these with environment variables.
rem ---------------------------------------------------------------------------
if "%WUI_VERSION_MAJOR%"=="" set WUI_VERSION_MAJOR=0
if "%WUI_VERSION_MINOR%"=="" set WUI_VERSION_MINOR=5
if "%WUI_VERSION_PATCH%"=="" set WUI_VERSION_PATCH=0
if "%WUI_VERSION_BUILD%"=="" set WUI_VERSION_BUILD=0

set VERSION_DEFS=/DWUI_VERSION_MAJOR=%WUI_VERSION_MAJOR% /DWUI_VERSION_MINOR=%WUI_VERSION_MINOR% /DWUI_VERSION_PATCH=%WUI_VERSION_PATCH% /DWUI_VERSION_BUILD=%WUI_VERSION_BUILD%

rem ---------------------------------------------------------------------------
rem Layout. Framework code and the sample are separate trees:
rem
rem   framework\src\core|mvvm|waml|render|platform   reusable UI/markup/MVVM engine
rem   framework\include                              (none yet; -I points at src)
rem   samples\gallery\                               one sample app
rem   tests\ bench\ tools\                           framework-level harnesses
rem
rem Sample code includes framework headers by plain name ("Mvvm.h"), so the
rem include path below is what makes a sample depend on the framework rather
rem than sit inside it.
rem ---------------------------------------------------------------------------
set FW=%SRC%framework
set SAMPLE=%SRC%samples\gallery

set COMMON_DEFS=/DNOMINMAX /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE %VERSION_DEFS% /I"%FW%\src" /I"%FW%\src\core" /I"%FW%\src\mvvm" /I"%FW%\src\waml" /I"%FW%\src\render" /I"%FW%\src\platform" /I"%SAMPLE%" /I"%SAMPLE%\resources" /I"%SRC%tests"
set COMMON=/nologo /c /std:c++20 /EHsc /utf-8 /W4 /permissive- /MP /Zc:__cplusplus /Zc:preprocessor %COMMON_DEFS%

rem Debug: symbols, no optimisation, markup from disk.
rem /MDd: debug CRT, to match /D_DEBUG. Mixing /MD with /D_DEBUG fails to link _CrtDbgReport.
set "CONFIG_DEBUG=/Od /Zi /DEBUG /MDd /D_DEBUG /DWUI_WAML_LOOSE_FILES=1"
rem Release: optimised, markup from resources, assertions off.
set "CONFIG_RELEASE=/O2 /Zi /MD /DNDEBUG"

rem Framework sources: everything here is UI toolkit, no sample knowledge.
set CORE=%FW%\src\core\Core.cpp %FW%\src\core\Widget.cpp %FW%\src\core\Types.cpp %FW%\src\core\Controls.cpp %FW%\src\mvvm\Binding.cpp %FW%\src\waml\Diagnostics.cpp %FW%\src\waml\Parser.cpp %FW%\src\render\SoftwareRender.cpp %FW%\src\platform\Resources.cpp
set RENDER=%FW%\src\render\D2DRender.cpp
set LIBS=d2d1.lib dwrite.lib comctl32.lib ole32.lib user32.lib gdi32.lib shell32.lib version.lib advapi32.lib

set TARGET=%1
if "%TARGET%"=="" set TARGET=release

rem Dispatch. Each branch sets the configuration, then jumps to the target
rem label. Kept as one flat list of one-line ifs: parenthesised blocks here
rem make cmd's %VAR% expansion surprising, which cost real debugging time.
if /i "%TARGET%"=="release"  set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_demo
if /i "%TARGET%"=="debug"    set "CFG_DIR=debug"   & set "CFG_FLAGS=%CONFIG_DEBUG%"   & goto :target_demo
if /i "%TARGET%"=="all"      set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_all
if /i "%TARGET%"=="tests"    set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_tests
if /i "%TARGET%"=="bench"    set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_bench
if /i "%TARGET%"=="snapshot" set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_snapshot
if /i "%TARGET%"=="interact" set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_interact
if /i "%TARGET%"=="probe"    set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_probe
if /i "%TARGET%"=="probedbg" set "CFG_DIR=debug"   & set "CFG_FLAGS=%CONFIG_DEBUG%"   & goto :target_probe
if /i "%TARGET%"=="res"      set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_res
if /i "%TARGET%"=="icon"     set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_icon
if /i "%TARGET%"=="dpishot"  set "CFG_DIR=release" & set "CFG_FLAGS=%CONFIG_RELEASE%" & goto :target_dpishot
echo Unknown target: %TARGET%
echo Valid: debug release all tests bench snapshot interact probe probedbg res icon dpishot
exit /b 1

rem ---------------------------------------------------------------------------
rem Per-configuration object directories so switching modes never reuses objects
rem compiled with a different WUI_WAML_LOOSE_FILES / optimisation setting.
rem ---------------------------------------------------------------------------
:target_res
call :do_res || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_icon
call :do_icon || exit /b 1
echo BUILD OK
exit /b 0

:target_tests
call :do_res || exit /b 1
call :do_tests || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_bench
call :do_res || exit /b 1
call :do_bench || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_snapshot
call :do_res || exit /b 1
call :do_snapshot || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_interact
call :do_res || exit /b 1
call :do_interact || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_dpishot
call :do_res || exit /b 1
call :do_dpishot || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_probe
call :do_res || exit /b 1
call :do_probe || exit /b 1
"%OUT%\%CFG_DIR%\waml_probe.exe" || exit /b 1
exit /b 0

:target_demo
call :do_res || exit /b 1
call :do_demo || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

:target_all
rem Each `call` is followed by a blank rem: cmd.exe resolves `call :label` from
rem a read buffer, and two consecutive calls straight after a block that ends in
rem `exit /b` can land on a buffer boundary and report
rem "cannot find the batch label". A no-op line between them keeps the lookup
rem inside one buffer. Measured: without it, `all` fails at :do_interact.
call :do_res || exit /b 1
rem
call :do_tests || exit /b 1
rem
call :do_bench || exit /b 1
rem
call :do_snapshot || exit /b 1
rem
call :do_interact || exit /b 1
rem
call :do_demo || exit /b 1
rem
call :do_probe || exit /b 1
echo BUILD OK (%CFG_DIR%)
exit /b 0

rem ---------------------------------------------------------------------------
rem Resource compilation.
rem
rem rc.exe resolves the .rc's relative paths against the .rc location, so the
rem invocation works from any cwd. A failure must abort: an exe that links
rem without its .res still runs but silently ships with no markup, no icon and
rem no version information.
rem ---------------------------------------------------------------------------
:do_res
if not exist "%SAMPLE%\resources\app.ico" (
    echo NOTE: resources\app.ico missing, generating
    call :do_icon || exit /b 1
)
if not exist "%OUT%\objres" mkdir "%OUT%\objres"
rc /nologo /fo"%OUT%\objres\app.res" /dWUI_VERSION_MAJOR=%WUI_VERSION_MAJOR% /dWUI_VERSION_MINOR=%WUI_VERSION_MINOR% /dWUI_VERSION_PATCH=%WUI_VERSION_PATCH% /dWUI_VERSION_BUILD=%WUI_VERSION_BUILD% "%SAMPLE%\resources\app.rc" || exit /b 1
exit /b 0

:do_icon
if not exist "%OUT%\objicon" mkdir "%OUT%\objicon"
cl %COMMON% %CONFIG_RELEASE% /Fo"%OUT%\objicon\\" "%SRC%tools\MakeIcon.cpp" || exit /b 1
link /nologo /OUT:"%OUT%\waml_makeicon.exe" /INCREMENTAL:NO "%OUT%\objicon\*.obj" gdiplus.lib ole32.lib user32.lib gdi32.lib || exit /b 1
"%OUT%\waml_makeicon.exe" "%SAMPLE%\resources\app.ico" || exit /b 1
exit /b 0

rem ---------------------------------------------------------------------------
rem Targets. Each keeps its objects under build\<config>\ to stay isolated.
rem ---------------------------------------------------------------------------
:do_tests
set OBJ=%OUT%\%CFG_DIR%\tests
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% "%SRC%tests\Tests.cpp" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_tests.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0

:do_bench
set OBJ=%OUT%\%CFG_DIR%\bench
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% "%SRC%bench\Bench.cpp" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_bench.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0

:do_snapshot
set OBJ=%OUT%\%CFG_DIR%\snapshot
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% %RENDER% "%SRC%tools\Snapshot.cpp" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_snapshot.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0

:do_demo
set OBJ=%OUT%\%CFG_DIR%\demo
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% %RENDER% "%SAMPLE%\App.cpp" || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_demo.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0

:do_interact
set OBJ=%OUT%\%CFG_DIR%\interact
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% "%SRC%tools\Interact.cpp" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_interact.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0

:do_dpishot
set OBJ=%OUT%\%CFG_DIR%\dpishot
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% %RENDER% "%SRC%tools\DpiShot.cpp" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_dpishot.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0

:do_probe
set OBJ=%OUT%\%CFG_DIR%\probe
if not exist "%OBJ%" mkdir "%OBJ%"
cl %COMMON% %CFG_FLAGS% /Fo"%OBJ%\\" %CORE% "%SRC%tools\ResourceProbe.cpp" || exit /b 1
link /nologo /DEBUG /OUT:"%OUT%\%CFG_DIR%\waml_probe.exe" /INCREMENTAL:NO /MANIFEST:EMBED /MANIFESTINPUT:"%SAMPLE%\resources\app.manifest" "%OBJ%\*.obj" "%OUT%\objres\app.res" %LIBS% || exit /b 1
exit /b 0
