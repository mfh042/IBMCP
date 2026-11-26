<#
.SYNOPSIS
  IBMCP one-shot test runner.
  1) eSync latest backend source (incl. tests) to Euler.
  2) Upload + execute remote run_tests.sh (smoke checks + collector unit tests).
  3) Print summary; exit with remote exit code (0 = all pass).
#>
$ErrorActionPreference = 'Continue'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# 临时抑制 euler-remote.ps1 的 BOM 无害报错（不影响函数加载），与 start-backend.ps1 保持一致
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'SilentlyContinue'
. (Join-Path $ScriptDir 'euler-remote.ps1')
$ErrorActionPreference = $prevEAP

Write-Host ''
Write-Host '========== IBMCP one-shot tests ==========' -ForegroundColor Cyan

# 1. sync latest source to Euler (now includes backend/collector/tests)
eSync
if ($LASTEXITCODE -ne 0) {
    Write-Host 'eSync failed, abort tests' -ForegroundColor Red
    exit 1
}

# 2. upload remote test script (normalize CRLF -> LF into a temp copy to avoid bash issues)
$localSh  = Join-Path $ScriptDir 'run_tests.sh'
$tmpSh    = Join-Path $env:TEMP 'ibmcp_run_tests_lf.sh'
$remoteSh = '/tmp/ibmcp_run_tests.sh'
# read as UTF-8 (auto-strips BOM), normalize to LF, write WITHOUT BOM so bash parses the shebang cleanly
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$shContent = Get-Content -Raw -Encoding UTF8 $localSh
$shContent = $shContent -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($tmpSh, $shContent, $utf8NoBom)
eup $tmpSh $remoteSh
if ($LASTEXITCODE -ne 0) {
    Write-Host 'upload run_tests.sh failed' -ForegroundColor Red
    exit 1
}

# 3. execute on remote
e "bash $remoteSh"
$rc = $LASTEXITCODE

Write-Host ''
if ($rc -eq 0) {
    Write-Host '>>> ALL TESTS PASSED <<<' -ForegroundColor Green
} else {
    Write-Host ">>> SOME TESTS FAILED (exit $rc) <<<" -ForegroundColor Red
}
exit $rc
