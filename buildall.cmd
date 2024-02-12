@echo off

call build /p:Configuration=Release /p:Platform=Win32 || goto :end
call build /p:Configuration=Release /p:Platform=x64
:end
exit /b %errorlevel%
