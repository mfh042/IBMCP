@echo off
setlocal
cd /d "%~dp0"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=5173"
echo %PORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 端口必须是数字: %PORT%
    echo 用法: stop-frontend.bat [端口]   默认 5173
    echo 按任意键关闭...
    pause >nul
    endlocal & exit /b 1
)

echo [IBMCP] 停止前端 (端口 %PORT%)
echo.

set "KILLED=0"
for /f "tokens=5" %%a in ('netstat -ano ^| findstr /C:":%PORT% " ^| findstr LISTENING') do (
    echo   终止进程 PID=%%a
    taskkill /F /PID %%a >nul 2>&1
    set "KILLED=1"
)

if "%KILLED%"=="0" (
    echo   未发现占用 %PORT% 端口的进程（前端可能未运行）
) else (
    echo [IBMCP] 前端已停止
)

echo.
echo 按任意键关闭窗口...
pause >nul
endlocal & exit /b 0