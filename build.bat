@echo off
setlocal
rem VS 2022 Build Tools (vcvars64 -> rc -> cl). Output: build\TLAP.exe
rem Usage: build.bat [clean] [icons] [trace] [tp]
rem   tp    = rebuild the third_party library (build\tp\tlap_tp.lib; otherwise built once)
rem   clean = delete build\ first
rem   icons = regenerate res\icon_*.png and res\app.ico from images\ (needs Python 3). Normally res\ is used as is.
rem   trace = also write build\TLAP_trace.log while running (for checking behaviour; not for release)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
set "CLEAN=" & set "ICONS=" & set "TRACE="
for %%A in (%*) do (
  if /i "%%A"=="clean" set "CLEAN=1"
  if /i "%%A"=="icons" set "ICONS=1"
  if /i "%%A"=="trace" set "TRACE=/DTLAP_TRACE"
  if /i "%%A"=="tp" set "TPREBUILD=1"
)
if defined CLEAN if exist build rmdir /s /q build
if not exist build mkdir build
if "%TPREBUILD%"=="1" del /q build\tp\tlap_tp.lib 2>nul
if not exist build\tp\tlap_tp.lib call tools\build_third_party.bat || exit /b 1
if defined ICONS (
  python tools\make_icons.py 2>nul || py tools\make_icons.py || (echo make_icons failed & exit /b 1)
)
rc /nologo /fo build\TLAP.res res\TLAP.rc || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /O1 /W3 /utf-8 /DUNICODE /D_UNICODE %TRACE% /Ithird_party\tlap_cfg /Ithird_party\opusfile-0.12\include /Ithird_party\libogg-1.3.6\include /Ithird_party\opus-1.6.1\include /Fo:build\ /Fe:build\TLAP.exe ^
  src\main.cpp src\ui.cpp src\audio.cpp src\resample.cpp src\tags.cpp src\eq.cpp src\vis.cpp src\decode.cpp src\folder.cpp src\explorer.cpp src\lang.cpp src\settings.cpp src\util.cpp src\zipfs.cpp src\colfit.cpp build\TLAP.res build\tp\tlap_tp.lib ^
  /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF || exit /b 1
if exist visualizers xcopy /y /i /q visualizers build\visualizers >nul
echo.
for %%F in (build\TLAP.exe) do echo TLAP.exe %%~zF bytes
endlocal
