@echo off
setlocal
cd /d "%~dp0"

set "BPORT=%~1"
set "FPORT=%~2"
if "%BPORT%"=="" set "BPORT=8000"
if "%FPORT%"=="" set "FPORT=5173"

echo %BPORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 后端端口必须是数字: %BPORT%
    echo 用法: start-all.bat [后端端口] [前端端口]   默认 8000 5173
    pause >nul
    endlocal & exit /b 1
)
echo %FPORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 前端端口必须是数字: %FPORT%
    pause >nul
    endlocal & exit /b 1
)

echo [IBMCP] 一键启动: 后端 ^(端口 %BPORT%^) + 前端 ^(端口 %FPORT%^)
echo   启动前若端口被占用会自动先停止占用进程。
echo.

REM 后端窗口: 同步源码 + 编译 + 重启服务 (首次需几分钟)
start "IBMCP-后端" cmd /k ""%~dp0start-backend.bat" build %BPORT% %FPORT%"

REM 稍等后端开始同步，再开前端
timeout /t 3 /nobreak >nul

REM 前端窗口: 本地 Vite dev server
start "IBMCP-前端" cmd /k ""%~dp0start-frontend.bat" %FPORT%"

echo [IBMCP] 已在两个新窗口启动:
echo   - 后端 ^(编译+运行^): http://192.168.80.136:%BPORT%
echo   - 前端 ^(Vite dev^):  http://localhost:%FPORT%
echo.
echo 后端编译完成后，浏览器打开 http://localhost:%FPORT% 即可联调。
echo 本窗口可关闭，子窗口会保留。
echo.
timeout /t 6 /nobreak >nul

endlocal & exit /b 0