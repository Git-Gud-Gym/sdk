@echo off
setlocal
rem Uses cmake from PATH when available (CI), otherwise the copy bundled with VS Build Tools.
set CMAKE=cmake
where cmake >nul 2>nul || set CMAKE="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
%CMAKE% -S "%~dp0." -B "%~dp0build" -G "Visual Studio 17 2022" -A x64 %* || exit /b 1
%CMAKE% --build "%~dp0build" --config Release || exit /b 1
