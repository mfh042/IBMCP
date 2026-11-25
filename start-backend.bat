@echo off
setlocal
cd /d "%~dp0"

set "MODE=%~1"
set "PORT=%~2"
set "FEPORT=%~3"
if "%MODE%"=="" set "MODE=build"

REM 若第一个参数是纯数字，则视为端口，模式取默认 build
echo %MODE%| findstr /r "^[0-9][0-9]*$" >nul
if not errorlevel 1 (
    set "PORT=%MODE%"
    set "MODE=build"
)

if /i "%MODE%"=="/build"   set "MODE=build"
if /i "%MODE%"=="/restart" set "MODE=restart"
if /i "%MODE%"=="/stop"    set "MODE=stop"
if /i "%MODE%"=="/logs"    set "MODE=logs"
if /i "%MODE%"=="/status"  set "MODE=status"
if /i "%MODE%"=="/?"  goto :help
if /i "%MODE%"=="-h"  goto :help

if "%PORT%"==""   set "PORT=8000"
if "%FEPORT%"=="" set "FEPORT=5173"

echo %PORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 后端端口必须是数字: %PORT%
    goto :help
)
echo %FEPORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 前端端口必须是数字: %FEPORT%
    goto :help
)

echo [IBMCP] 后端快速启动  模式=%MODE%  端口=%PORT%  前端端口=%FEPORT%
echo [IBMCP] 项目根: %cd%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deployments\openeuler\start-backend.ps1" -Mode %MODE% -Port %PORT% -FrontendPort %FEPORT%
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo [IBMCP] 执行失败 ^(exit %RC%^)
    echo 按任意键关闭窗口...
    pause >nul
)
endlocal & exit /b %RC%

:help
echo IBMCP 后端快速启动 ^(openEuler 远程编译 + systemd 重启^)
echo.
echo  用法:
echo    start-backend.bat [模式] [后端端口] [前端端口]
echo.
echo  模式 ^(默认 build^):
echo    start-backend.bat                同步源码 + 编译 + 重启   ^(端口 8000^)
echo    start-backend.bat /build         同上
echo    start-backend.bat /restart       仅重启服务 ^(代码未改时较快^)
echo    start-backend.bat /stop          停止服务
echo    start-backend.bat /status        查看状态 + 健康检查
echo    start-backend.bat /logs          查看最近 50 行日志
echo.
echo  端口 ^(默认 后端 8000 / 前端 5173^):
echo    start-backend.bat 8001           build 模式，后端监听 8001
echo    start-backend.bat /restart 8001  restart 模式，后端监听 8001
echo    start-backend.bat /build 8000 5174   后端 8000，前端 cors 放行 5174
echo.
echo  说明: 启动前若端口被占用会自动先停止占用进程再启动。
echo  依赖: deployments\openeuler\euler-remote.ps1 ^(需先 eInitKey 配 SSH 免密^)
endlocal & exit /b 0