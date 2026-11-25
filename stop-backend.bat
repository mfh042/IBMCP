@echo off
setlocal
cd /d "%~dp0"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=8000"
echo %PORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 端口必须是数字: %PORT%
    echo 用法: stop-backend.bat [端口]   默认 8000
    echo 按任意键关闭...
    pause >nul
    endlocal & exit /b 1
)

echo [IBMCP] 停止后端 (openEuler 远程 ibmcp-api, 端口 %PORT%)
echo [IBMCP] 项目根: %cd%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deployments\openeuler\start-backend.ps1" -Mode stop -Port %PORT%
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo [IBMCP] 执行失败 ^(exit %RC%^)
)
echo 按任意键关闭窗口...
pause >nul
endlocal & exit /b %RC%