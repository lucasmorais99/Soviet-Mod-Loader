@echo off
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
set PF86=%ProgramFiles(x86)%
if not defined PF86 set PF86=C:\Program Files (x86)
set VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VCVARS%" if exist "%VSWHERE%" for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" echo [test] Visual Studio C++ toolset not found & exit /b 1
call "%VCVARS%" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\tests mkdir build\tests
set COMMON=/nologo /O2 /MT /W4 /EHsc /std:c++17 /Iinclude /c
cl %COMMON% /Fo"build\tests\core_tests.obj" tests\core_tests.cpp
if errorlevel 1 exit /b 1
cl %COMMON% /DTsmPluginApiVersion=SmlBuildingsApiVersion /DTsmPluginInit=SmlBuildingsInit /DDllMain=SmlBuildingsDllMain /Fo"build\tests\buildings.obj" vendor\tesmio\buildings.cpp
if errorlevel 1 exit /b 1
cl %COMMON% /DTsmPluginApiVersion=SmlResourcesApiVersion /DTsmPluginInit=SmlResourcesInit /DDllMain=SmlResourcesDllMain /Fo"build\tests\resources.obj" vendor\tesmio\resources.cpp
if errorlevel 1 exit /b 1
cl %COMMON% /DTsmPluginApiVersion=SmlDepositsApiVersion /DTsmPluginInit=SmlDepositsInit /DDllMain=SmlDepositsDllMain /Fo"build\tests\deposits.obj" vendor\tesmio\deposits.cpp
if errorlevel 1 exit /b 1
cl %COMMON% /DTsmPluginApiVersion=SmlNeedsApiVersion /DTsmPluginInit=SmlNeedsInit /DTsmPluginStart=SmlNeedsStart /DDllMain=SmlNeedsDllMain /Fo"build\tests\needs.obj" vendor\tesmio\needs.cpp
if errorlevel 1 exit /b 1
link /nologo /OUT:"build\tests\core_tests.exe" build\tests\core_tests.obj ^
  build\tests\buildings.obj build\tests\resources.obj build\tests\deposits.obj ^
  build\tests\needs.obj kernel32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
build\tests\core_tests.exe
exit /b %errorlevel%
