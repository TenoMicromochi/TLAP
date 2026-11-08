@echo off
setlocal
rem Builds tools\encode.cpp and tools\probe.cpp into tools\bin\ (test helpers, not shipped)
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /MT /O1 /W3 /utf-8 /DUNICODE /D_UNICODE /Fo:bin\ /Fe:bin\encode.exe encode.cpp || exit /b 1
cl /nologo /std:c++17 /EHsc /MT /O1 /W3 /utf-8 /DUNICODE /D_UNICODE /Fo:bin\ /Fe:bin\probe.exe probe.cpp || exit /b 1
endlocal
