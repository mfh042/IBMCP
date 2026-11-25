@echo off
setlocal
cd /d "%~dp0"

set "BPORT=%~1"
set "FPORT=%~2"
if "%BPORT%"=="" set "BPORT=8000"
if "%FPORT%"=="" set "FPORT=5173"

echo [IBMCP] 一键停止: 后端 ^(端口 %BPORT%^) + 前端 ^(端口 %FPORT%^)
echo.

echo ===== 1/2 停止后端 =====
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deployments\openeuler\start-backend.ps1" -Mode stop -Port %BPORT%

echo.
echo ===== 2/2 停止前端 =====
set "KILLED=0"
for /f "tokens=5" %%a in ('netstat -ano ^| findstr /C:":%FPORT% " ^| findstr LISTENING') do (
    echo   终止进程 PID=%%a
    taskkill /F /PID %%a >nul 2>&1
    set "KILLED=1"
)
if "%KILLED%"=="0" (
    echo   未发现占用 %FPORT% 端口的进程（前端可能未运行）
) else (
    echo   前端已停止
)

echo.
echo [IBMCP] 前后端停止流程完成
timeout /t 4 /nobreak >nul
endlocal & exit /b 0