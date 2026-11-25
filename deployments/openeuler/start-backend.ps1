# =============================================================================
# IBMCP 后端启动辅助脚本（Windows 11 开发侧）
# 由项目根 start-backend.bat / stop-backend.bat 调用；也可直接运行：
#   powershell -NoProfile -ExecutionPolicy Bypass -File start-backend.ps1 -Mode build -Port 8000
# 依赖: 同目录 euler-remote.ps1（需先 eInitKey 配置 SSH 免密）
# =============================================================================
param(
    [ValidateSet('build', 'restart', 'stop', 'logs', 'status')]
    [string]$Mode = 'build',
    [int]$Port = 8000,
    [int]$FrontendPort = 5173
)

$ErrorActionPreference = 'Continue'

# 项目根: 本脚本位于 deployments/openeuler/，上两级即项目根
$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
$RemoteRoot  = '/home/meng/ibmcp'
$RemoteTool  = "$RemoteRoot/deployments/openeuler"
$PingUrl     = "http://127.0.0.1:$Port/api/v1/ping"

Write-Host ''
Write-Host '===== IBMCP 后端启动 =====' -ForegroundColor Yellow
Write-Host "  模式       : $Mode"
Write-Host "  项目根     : $ProjectRoot"
Write-Host "  目标机     : meng@192.168.80.136"
Write-Host "  远程根     : $RemoteRoot"
Write-Host "  监听端口   : $Port"
Write-Host "  前端端口   : $FrontendPort (用于 cors_origins)"
Write-Host ''

$toolPs1 = Join-Path $ScriptDir 'euler-remote.ps1'
if (-not (Test-Path $toolPs1)) {
    Write-Error "未找到远程工具: $toolPs1"
    exit 1
}
# dot-source 加载 e / esudo / eSync 等函数
# 临时抑制 euler-remote.ps1 文件 BOM 被误解析产生的无害报错（不影响函数加载）
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'SilentlyContinue'
. $toolPs1
$ErrorActionPreference = $prevEAP

function Test-BackendHealth {
    Write-Host '[健康检查] 探测' $PingUrl '...' -ForegroundColor Cyan
    $raw = e "curl -s -m 5 $PingUrl" 2>&1
    $txt = ($raw | Out-String).Trim()
    if ($txt -match '"code"\s*:\s*0') {
        Write-Host '  后端在线: ' -NoNewline -ForegroundColor Green
        Write-Host $txt
        return $true
    }
    Write-Host '  未就绪: ' -NoNewline -ForegroundColor Red
    Write-Host $txt
    return $false
}

# 清理远程占用指定端口的进程（优先 fuser，回退 ss 解析 pid）
function Clear-RemotePort {
    param([int]$P)
    Write-Host "[端口] 清理远程占用 $P 端口的残留进程 ..." -ForegroundColor Cyan
    $cmd = 'bash -c ''P=' + $P + '; if command -v fuser >/dev/null 2>&1; then fuser -k ${P}/tcp 2>/dev/null; else for p in $(ss -ltnp 2>/dev/null | grep ":' + $P + ' " | grep -o "pid=[0-9]*" | cut -d= -f2); do kill -9 $p 2>/dev/null; done; fi; sleep 1'''
    esudo $cmd
}

# 修改后端监听端口 & 前端 cors 来源（改本地源码 config.json + 同步到远程 dist/config.json）
function Set-BackendPort {
    param([int]$P, [int]$Fep)
    $cfgLocal = Join-Path $ProjectRoot 'backend\config.json'
    if (-not (Test-Path $cfgLocal)) {
        Write-Warning "未找到 $cfgLocal，跳过端口配置"
        return
    }
    $txt = [IO.File]::ReadAllText($cfgLocal)
    $changed = $false

    # 1. listeners 端口：只替换第一个 "port": N（db 的 5432 在后面，不受影响）
    $m = [regex]::Match($txt, '"port"\s*:\s*(\d+)')
    if ($m.Success -and [int]$m.Groups[1].Value -ne $P) {
        Write-Host "[端口] 监听端口 $($m.Groups[1].Value) -> $P" -ForegroundColor Cyan
        $txt = [regex]::Replace($txt, '("port"\s*:\s*)\d+', "`${1}$P", 1)
        $changed = $true
    }

    # 2. cors_origins 加入前端地址 http://localhost:<前端端口>
    $origin = "http://localhost:$Fep"
    $cors = [regex]::Match($txt, '"cors_origins"\s*:\s*\[(.*?)\]', [Text.RegularExpressions.RegexOptions]::Singleline)
    if ($cors.Success) {
        $inside = $cors.Groups[1].Value
        if ($inside -notlike "*$origin*") {
            Write-Host "[端口] cors_origins 加入 $origin" -ForegroundColor Cyan
            $newInside = $inside.Trim()
            if ($newInside.Length -gt 0) { $newInside = $newInside.TrimEnd(',') + ', ' }
            $newInside += "`"$origin`""
            $g = $cors.Groups[1]
            $txt = $txt.Substring(0, $g.Index) + $newInside + $txt.Substring($g.Index + $g.Length)
            $changed = $true
        }
    }

    if ($changed) {
        [IO.File]::WriteAllText($cfgLocal, $txt)
        eup $cfgLocal "$RemoteRoot/dist/config.json" | Out-Null
        Write-Host "  已更新本地 config.json 并同步到远程 dist/config.json" -ForegroundColor Green
    } else {
        Write-Host "  端口配置已是目标值（监听 $P / cors 含 $origin），无需修改" -ForegroundColor DarkGray
    }
}

function Restart-Service {
    param([int]$P)
    Write-Host '[服务] 停止服务 -> 清理端口占用 -> 启动 ibmcp-api ...' -ForegroundColor Cyan
    $cmd = 'bash -c ''systemctl stop ibmcp-api; P=' + $P + '; if command -v fuser >/dev/null 2>&1; then fuser -k ${P}/tcp 2>/dev/null; else for p in $(ss -ltnp 2>/dev/null | grep ":' + $P + ' " | grep -o "pid=[0-9]*" | cut -d= -f2); do kill -9 $p 2>/dev/null; done; fi; sleep 2; systemctl start ibmcp-api'''
    esudo $cmd
    Start-Sleep -Seconds 4
    $active = (e 'systemctl is-active ibmcp-api' 2>&1 | Out-String).Trim()
    Write-Host "  ibmcp-api 状态: $active"
    if ($active -ne 'active') {
        Write-Host '  服务未 active，最近日志:' -ForegroundColor Red
        esudo 'journalctl -u ibmcp-api -n 20 --no-pager'
    }
    Test-BackendHealth | Out-Null
}

switch ($Mode) {
    'build' {
        Write-Host '[1/3] 同步本地源码 -> 远程 (eSync) ...' -ForegroundColor Cyan
        Set-BackendPort -P $Port -Fep $FrontendPort
        eSync
        if (-not $?) { Write-Error 'eSync 失败，已中止'; exit 1 }

        Write-Host ''
        Write-Host '[2/3] 远程编译 (build.sh, Release) ...' -ForegroundColor Cyan
        Write-Host "  产物 -> $RemoteRoot/dist/bin/ibmcp_server  (监听 $Port)"
        e "cd $RemoteRoot && bash $RemoteTool/build.sh"
        if (-not $?) { Write-Error '远程编译失败，已中止'; exit 1 }

        Write-Host ''
        Write-Host '[3/3] 重启 systemd 服务 ...' -ForegroundColor Cyan
        Restart-Service -P $Port
    }
    'restart' {
        Set-BackendPort -P $Port -Fep $FrontendPort
        Restart-Service -P $Port
    }
    'stop' {
        Write-Host '[服务] 停止 ibmcp-api ...' -ForegroundColor Cyan
        esudo 'bash -c ''systemctl stop ibmcp-api; pkill -9 -x ibmcp_server 2>/dev/null'''
        Clear-RemotePort -P $Port
        Write-Host "已停止 ibmcp-api (端口 $Port)" -ForegroundColor Green
        Test-BackendHealth | Out-Null
    }
    'logs' {
        esudo 'journalctl -u ibmcp-api -n 50 --no-pager'
    }
    'status' {
        esudo 'systemctl status ibmcp-api --no-pager -n 10'
        Test-BackendHealth | Out-Null
    }
}

Write-Host ''
Write-Host "===== 完成 (模式: $Mode, 端口: $Port) =====" -ForegroundColor Yellow