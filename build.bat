@echo off
where cl >nul 2>nul || (echo Run this from the x64 Native Tools Command Prompt for VS & pause & exit /b 1)

echo [1/3] external exe
cl /nologo /std:c++20 /O2 /MT /EHsc /DROBRUN_INTERNAL=0 robrun.cpp /Fe:robrun.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib || goto fail

echo [2/3] internal dll
cl /nologo /std:c++20 /O2 /MT /EHa /DROBRUN_INTERNAL=1 robrun.cpp /LD /Fe:robrun.dll ^
   /link user32.lib gdi32.lib || goto fail

echo [3/3] injector
cl /nologo /std:c++20 /O2 /MT inject.cpp /Fe:inject.exe || goto fail

echo.
echo done: robrun.exe  robrun.dll  inject.exe
pause & exit /b 0

:fail
echo build failed
pause & exit /b 1