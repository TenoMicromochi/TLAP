@echo off
setlocal
rem Release build: clean build -> ISCC -> dist\ (TLAPSetup-<version>.exe, TLAP-<version>-portable.zip, Readme.txt, LICENSE, THIRD_PARTY_NOTICES.txt)
rem The version comes from src\version.h (through the exe).
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
cd /d "%~dp0"
if not exist "%ISCC%" goto noiscc
call "%~dp0build.bat" clean || goto fail
if exist dist rmdir /s /q dist
mkdir dist
"%ISCC%" /Qp installer\TLAP.iss || goto fail
rem Portable zip: TLAP\ folder with exe, visualizers, docs
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Get-Item 'build\TLAP.exe').VersionInfo.ProductVersion"`) do set "VER=%%V"
if "%VER%"=="" goto fail
set "STAGE=%TEMP%\TLAP_portable_stage"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%\TLAP\visualizers" || goto fail
copy /y build\TLAP.exe "%STAGE%\TLAP\TLAP.exe" >nul || goto fail
xcopy /y /q visualizers\* "%STAGE%\TLAP\visualizers\" >nul || goto fail
copy /y installer\Readme.txt "%STAGE%\TLAP\Readme.txt" >nul || goto fail
copy /y LICENSE "%STAGE%\TLAP\LICENSE" >nul || goto fail
copy /y THIRD_PARTY_NOTICES.txt "%STAGE%\TLAP\THIRD_PARTY_NOTICES.txt" >nul || goto fail
rem Windows built-in bsdtar: entry paths use "/" (PowerShell 5.1 Compress-Archive writes "\")
"%SystemRoot%\System32\tar.exe" -a -c -f "%~dp0dist\TLAP-%VER%-portable.zip" -C "%STAGE%" TLAP || goto fail
rmdir /s /q "%STAGE%"
copy /y installer\Readme.txt dist\Readme.txt >nul || goto fail
copy /y LICENSE dist\LICENSE >nul || goto fail
copy /y THIRD_PARTY_NOTICES.txt dist\THIRD_PARTY_NOTICES.txt >nul || goto fail
echo.
dir /b dist
exit /b 0
:noiscc
echo ISCC not found
exit /b 1
:fail
echo release failed
exit /b 1
