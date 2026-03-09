@echo off
set "LOGFILE=%~dp0\call_log.txt"
echo [%date% %time%] Executable: %0 >> "%LOGFILE%"
echo [%date% %time%] Parameters: %* >> "%LOGFILE%"
echo ---------------------------------------- >> "%LOGFILE%"
