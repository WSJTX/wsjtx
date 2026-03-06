@echo off
setlocal EnableDelayedExpansion

:: Define the configuration name here
set CONFIG_NAME=JTTY
set WINDOW_HANDLE=

:: Parse incoming arguments
:parse_args
if "%~1" == "" goto run_wsjtx

set "arg=%~1"

:: Check if the argument starts with "-h" (e.g., -h0DED)
if /I "!arg:~0,2!" == "-h" (
    :: Extract the window handle trailing after "-h"
    set "WINDOW_HANDLE=!arg:~2!"
) else if /I "!arg!" == "-h" (
    :: Handle the case where the handle is separated by a space (e.g., -h 0DED)
    set "WINDOW_HANDLE=%~2"
    shift
)

:: "-r" is effectively ignored as we just shift past any arguments we don't explicitly handle.
shift
goto parse_args

:run_wsjtx
if defined WINDOW_HANDLE (
    start "" "wsjtx.exe" --config "%CONFIG_NAME%" --window-handle "!WINDOW_HANDLE!"
) else (
    start "" "wsjtx.exe" --config "%CONFIG_NAME%"
)

endlocal
