@echo off
setlocal
rem Builds nosleep.exe with the vendored TinyCC. No arguments, no environment
rem setup, nothing to install. The tests run first and a failure stops the
rem build.

cd /d "%~dp0"

set TCC=tools\tcc\tcc.exe
if not exist "%TCC%" (
  echo.
  echo   TinyCC is missing. Run this once to fetch it:
  echo     powershell -ExecutionPolicy Bypass -File tools\get-tcc.ps1
  echo.
  exit /b 1
)

if not exist build mkdir build

echo.
echo   [1/2] tests
"%TCC%" -Wall tests\test_activity.c -o build\test_activity.exe
if errorlevel 1 (
  echo   test build FAILED
  exit /b 1
)
build\test_activity.exe
if errorlevel 1 (
  echo   tests FAILED -- nosleep.exe was not built
  exit /b 1
)

rem Not shipped: a console harness for watching a real process tick by tick.
"%TCC%" -Wall tests\probe.c -o build\probe.exe -lkernel32 -luser32 ^
  build\kernel32ext.def >nul 2>&1

echo.
echo   [2/2] nosleep.exe

rem The .def files under build\ supply the handful of imports TinyCC's own
rem kernel32.def is missing, plus shell32, advapi32 and one user32 symbol
rem that it omits entirely.
"%TCC%" -Wall ^
  src\app.c src\ui.c src\theme.c src\tray.c src\applist.c ^
  src\monitor.c src\tracker.c src\activity.c src\power.c ^
  src\netstat.c src\logfile.c ^
  -o nosleep.exe ^
  -Wl,-subsystem=windows ^
  -lkernel32 -luser32 -lgdi32 ^
  build\kernel32ext.def build\shell32.def build\advapi32.def ^
  build\user32ext.def
if errorlevel 1 (
  echo   build FAILED
  exit /b 1
)

for %%F in (nosleep.exe) do echo   nosleep.exe  %%~zF bytes
echo.
endlocal
