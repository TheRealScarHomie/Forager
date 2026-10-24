@echo off
setlocal
rem needs vs 2022 build tools
rem   build.bat        tests dll and zip
rem   build.bat testing only

set "VCVARS=E:\2 - programs\VS2022BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul 2>nul
if errorlevel 1 (echo could not set up the compiler from "%VCVARS%" & exit /b 1)

cd /d "%~dp0"
set "NAME=Forager"
set "VERSION=1.0.0"
if not exist build mkdir build

echo [1/4] unit tests
if not exist tests\tests.cpp goto notests
cl /nologo /std:c++20 /EHsc /W4 /O2 /Fo:build\ /Fe:build\tests.exe tests\tests.cpp src\rules.cpp src\config.cpp >build\tests_build.log 2>&1
if errorlevel 1 (type build\tests_build.log & exit /b 1)
build\tests.exe
if errorlevel 1 (echo unit tests failed & exit /b 1)
:notests
if /i "%~1"=="tests" exit /b 0

echo [2/4] UE4SS import library
lib /nologo /def:src\ue4ss.def /machine:x64 /out:build\UE4SS.lib >build\lib.log 2>&1
if errorlevel 1 (type build\lib.log & exit /b 1)

echo [3/4] main.dll
cl /nologo /std:c++20 /EHa /MD /O2 /Zi /W3 /wd4251 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Fo:build\ /Fd:build\vc.pdb /LD ^
   src\mod.cpp src\engine.cpp src\rules.cpp src\config.cpp ^
   /Fe:build\main.dll /link build\UE4SS.lib /DEBUG /PDB:build\main.pdb /PDBALTPATH:main.pdb /OPT:REF /OPT:ICF >build\dll_build.log 2>&1
if errorlevel 1 (type build\dll_build.log & exit /b 1)

echo [4/4] dist\%NAME% and the zip
if exist dist\%NAME% rmdir /s /q dist\%NAME%
mkdir dist\%NAME%\dlls
copy /y build\main.dll dist\%NAME%\dlls\main.dll >nul
copy /y config.ini dist\%NAME%\config.ini >nul
type nul > dist\%NAME%\enabled.txt
if exist build\zip rmdir /s /q build\zip
mkdir build\zip\Mods
xcopy /e /i /q /y dist\%NAME% build\zip\Mods\%NAME% >nul
if exist dist\*.zip del /q dist\*.zip
"%SystemRoot%\System32\tar.exe" -a -c -f dist\%NAME%-%VERSION%.zip -C build\zip Mods
if errorlevel 1 (echo could not create the zip & exit /b 1)
echo build ok: dist\%NAME% and dist\%NAME%-%VERSION%.zip
endlocal
