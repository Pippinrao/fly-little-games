# FlyNES 工具链: 安装 mobile-mcp（MCP 移动设备自动化服务器）
# 用法: powershell -ExecutionPolicy Bypass -File scripts/setup-mobile-mcp.ps1
# 说明:
#   - 上游 npm 包 @mobilenext/mobile-mcp 依赖不存在的 mobilecli@1.0.0（发布 bug），
#     本脚本用 npm overrides 强制 mobilecli -> 0.3.88（真实最新版）修复。
#   - 安装在 $env:USERPROFILE\.mobile-mcp-install（专用目录，不动全局 node_modules）。
#   - 本机 HTTP_PROXY 会让 localhost/::1 请求被路由到代理而失败，客户端配置需保证
#     NO_PROXY 含 ::1 / localhost，或直接使用 stdio 模式（不经 HTTP）。

$ErrorActionPreference = "Stop"
$dir = "$env:USERPROFILE\.mobile-mcp-install"
$entry = "$dir\node_modules\@mobilenext\mobile-mcp\lib\index.js"

Write-Host "==> 1/3 安装依赖（overrides 修复 mobilecli 版本）"
if (-not (Test-Path "$dir\package.json")) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Set-Content -Path "$dir\package.json" -Encoding utf8 -Value @'
{
  "name": "mobile-mcp-install",
  "private": true,
  "version": "1.0.0",
  "dependencies": {
    "@mobilenext/mobile-mcp": "1.0.2"
  },
  "overrides": {
    "mobilecli": "0.3.88"
  }
}
'@
}
Push-Location $dir
npm install --no-fund --no-audit
Pop-Location

Write-Host "==> 2/3 验证入口"
if (-not (Test-Path $entry)) { Write-Error "安装失败: 未找到 $entry"; exit 1 }
Write-Host "OK: $entry"

Write-Host "==> 3/3 快速验证（SSE 模式 + MCP 客户端握手）"
$env:MOBILEMCP_DISABLE_TELEMETRY = "1"
$log = "$env:TEMP\mobile-mcp-verify.log"
$node = (Get-Command node).Source
Start-Process -FilePath $node -ArgumentList "`"$entry`"","--listen","3123" -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError "$log.err"
Start-Sleep -Seconds 4
try {
    $devices = curl.exe --noproxy "*" -s --max-time 5 "http://[::1]:3123/mcp" 2>&1
    if ($devices -match "sessionId") { Write-Host "OK: SSE 服务响应正常（sessionId 已获取）" } else { Write-Host "WARN: 服务未按预期响应（不影响 stdio 模式使用）" }
} catch { Write-Host "WARN: 验证请求失败: $($_.Exception.Message)" }
Get-NetTCPConnection -LocalPort 3123 -State Listen -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }

Write-Host ""
Write-Host "==> 安装完成。MCP 客户端配置："
Write-Host "  stdio 模式（推荐，Claude Code / Cursor / Codex 等）:"
Write-Host "    command: node"
Write-Host "    args: [ `"$entry`" ]"
Write-Host "    环境: MOBILEMCP_DISABLE_TELEMETRY=1"
Write-Host "  SSE 模式:"
Write-Host "    node `"$entry`" --listen 3000   （绑定 IPv6 ::1；客户端连 http://[::1]:3000/mcp 或 http://localhost:3000/mcp）"
Write-Host "  npx 方式（若上游修复后）: npx -y @mobilenext/mobile-mcp@latest"
Write-Host ""
Write-Host "验证: 配置后在客户端里问 AI “列出可用设备”，应返回你的手机（如 V2324A）。"
