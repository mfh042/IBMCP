param([string]$Python = "python")

Write-Host "[IBMCP] 创建 venv ..." -ForegroundColor Cyan
& $Python -m venv .venv
.\.venv\Scripts\Activate.ps1

Write-Host "[IBMCP] 升级 pip ..." -ForegroundColor Cyan
pip install --upgrade pip

Write-Host "[IBMCP] 安装 backend + dev 依赖 ..." -ForegroundColor Cyan
pip install -e .\backend[dev]

Write-Host "[IBMCP] ✅ Windows 开发环境就绪" -ForegroundColor Green
Write-Host "   Windows 下 collector 后端默认为 stub（无真实 IO）"
Write-Host "   在 backend\.env 中设置 COLLECTOR_BACKEND=stub"