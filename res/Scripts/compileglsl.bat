@echo off
setlocal
set "SOURCE=%~dp0..\Shaders\Source"
set "OUTPUT=%~dp0..\Shaders\Bin"
if not exist "%OUTPUT%" mkdir "%OUTPUT%"
glslc "%SOURCE%\vert.vert" -o "%OUTPUT%\vert.spv" || exit /b 1
glslc "%SOURCE%\frag.frag" -o "%OUTPUT%\frag.spv" || exit /b 1
