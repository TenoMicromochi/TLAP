@echo off
setlocal
rem Builds and runs tools\test_colfit.cpp (column width logic, no UI). Exit code 0 = all passed
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || (echo vcvars64 failed & exit /b 1)
cd /d "%~dp0"
if not exist bin mkdir bin
cl /nologo /std:c++17 /EHsc /MT /O1 /W3 /utf-8 /DUNICODE /D_UNICODE /Fo:bin\ /Fe:bin\test_colfit.exe test_colfit.cpp ..\src\colfit.cpp || exit /b 1
bin\test_colfit.exe || exit /b 1
endlocal
