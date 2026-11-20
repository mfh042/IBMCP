param([string]$PythonVersion = "3.11.18")

# =============================================================================
# IBMCP Windows 11 开发环境一键初始化（uv 管理 Python）
# 用法：powershell -ExecutionPolicy Bypass -File deployments\dev-windows\bootstrap.ps1
#
# 设计口径：
#   - Python 解释器由 uv 托管（独立于系统 Python），版本锁定 backend/.python-version
#   - 依赖按 uv.lock 精确同步（uv sync），无需手工 pip install
#   - Windows 侧只跑 stub，不编译 _core.so（C++ 扩展在欧拉编译，见 08 号文档）
# =============================================================================

$ErrorActionPreference = "Stop"
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $ProjectRoot

function Refresh-Path {
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("Path", "User")
}

Write-Host "[IBMCP] 检查 uv ..." -ForegroundColor Cyan
if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    Write-Host "[IBMCP] 未检测到 uv，正在安装（官方安装脚本）..." -ForegroundColor Yellow
    powershell -ExecutionPolicy ByPass -NoProfile -Command "irm https://astral.sh/uv/install.ps1 | iex"
    Refresh-Path
    if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
        Write-Host "❌ uv 安装失败或不在 PATH，请重启终端后重试，或手动安装：irm https://astral.sh/uv/install.ps1 | iex" -ForegroundColor Red
        exit 1
    }
}
Write-Host "   uv: $(uv --version)"

Write-Host "[IBMCP] 安装 Python $PythonVersion（uv 托管，不污染系统 Python）..." -ForegroundColor Cyan
uv python install $PythonVersion

Write-Host "[IBMCP] 创建 .venv 并按 uv.lock 同步依赖（含 dev 组：pytest/ruff/mypy）..." -ForegroundColor Cyan
uv sync

Write-Host ""
Write-Host "[IBMCP] ✅ Windows 开发环境就绪" -ForegroundColor Green
Write-Host "   Python : $(uv run python --version 2>&1)"
Write-Host "   venv   : $ProjectRoot\.venv"
Write-Host "   验证   : uv run python -c `"import fastapi, uvicorn, asyncpg; print('deps OK')`""
Write-Host ""
Write-Host "   ⚠️  Windows 下 collector 后端只能用 stub（无真实 IO、无 _core.so）"
Write-Host "       请在 backend\.env 中确认 COLLECTOR_BACKEND=stub"
Write-Host "   启动   : cd backend; uv run uvicorn app.main:app --reload --host 127.0.0.1 --port 8000"