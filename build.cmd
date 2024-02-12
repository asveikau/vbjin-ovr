@echo off

where msbuild >NUL 2>&1 && goto :have-msbuild

rem Find "vcvarsall.bat" from Visual Studio.
rem This part is borrowed from git@github.com:asveikau/windows-rcscript.git
rem
echo Looking for Visual Studio ...
pushd .
cd /d "%ProgramFiles(x86)%"
for /f "tokens=*" %%x in ('dir /s vcvarsall.bat /b') do set VCVARSALL=%%x
popd
if "%VCVARSALL%"=="" goto :fail-vcvarsall
echo Running vcvarsall.bat ...
"%VCVARSALL%"
set VCVARSALL=

rem Double check for MSBUILD
where msbuild >NUL 2>&1 || goto :fail-msbuild

:have-msbuild

msbuild vc10_vbjin.sln %*
exit /b %errorlevel%

:fail-vcvarsall
echo Could not find vcvarsall.bat. 1>&2
goto :fail
:fail-msbuild
echo Could not find msbuild. 1>&2
:fail
exit /b 1
