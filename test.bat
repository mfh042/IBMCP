@chcp 65001>nul
@echo off
setlocal
cd /d "%~dp0"
echo [IBMCP] 运行测试：同步源码到欧拉 + 冒烟检查 + 采集器单元测试
echo        需要能免密 SSH 到 192.168.80.136（eInitKey 已配置）。
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deployments\openeuler\run-tests.ps1"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
    echo [IBMCP] 测试完成：全部通过
) else (
    echo [IBMCP] 测试完成：存在失败 ^(exit %RC%^)
)
endlocal & exit /b %RC%
