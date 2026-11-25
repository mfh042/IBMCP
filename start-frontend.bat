@echo off
setlocal
cd /d "%~dp0frontend"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=5173"
echo %PORT%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 端口必须是数字: %PORT%
    echo 用法: start-frontend.bat [端口]   默认 5173
    echo 按任意键关闭...
    pause >nul
    endlocal & exit /b 1
)

echo [IBMCP] 前端快速启动  端口=%PORT%
echo [IBMCP] 工作目录: %cd%
echo.

where npm >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到 npm，请先安装 Node.js ^(https://nodejs.org^)
    echo 按任意键关闭...
    pause >nul
    endlocal & exit /b 1
)

if not exist "node_modules" (
    echo [前端] 未找到 node_modules，正在安装依赖 ^(首次较慢^)...
    call npm install
    if errorlevel 1 (
        echo [前端] npm install 失败，按任意键关闭...
        pause >nul
        endlocal & exit /b 1
    )
)

findstr /C:"VITE_API_TARGET=http://192.168.80.136:8000" ".env.development" >nul
if errorlevel 1 (
    echo [警告] .env.development 的 VITE_API_TARGET 未指向远程后端！
    echo        应为: VITE_API_TARGET=http://192.168.80.136:8000
    echo        否则前端将无法连接后端。
    echo.
)

echo [前端] 检查端口 %PORT% 占用情况...
set "OCCUPIED=0"
for /f "tokens=5" %%a in ('netstat -ano ^| findstr /C:":%PORT% " ^| findstr LISTENING') do (
    echo   端口 %PORT% 被 PID=%%a 占用，先停止...
    taskkill /F /PID %%a >nul 2>&1
    set "OCCUPIED=1"
)
if "%OCCUPIED%"=="0" echo   端口 %PORT% 空闲
timeout /t 1 /nobreak >nul

echo.
echo [前端] 启动 Vite dev server
echo [前端] 访问地址 : http://localhost:%PORT%
echo [前端] API 代理 : http://192.168.80.136:8000
echo [前端] 停止请按 Ctrl+C
echo.

call npm run dev -- --port %PORT% --strictPort

endlocal & exit /b %ERRORLEVEL%