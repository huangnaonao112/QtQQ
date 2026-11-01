# =============================================================================
#  e2e.ps1 — 一键端到端回归（不需要人工点界面）
#
#  覆盖：服务端转发、机器人应答、两个账号互发、AI 私聊、断线重连、
#        消息落库+历史、离线私聊补发。
#
#  用法：powershell -NoProfile -ExecutionPolicy Bypass -File scripts\e2e.ps1
#  结果：逐项 PASS/FAIL，退出码 0=全过，1=有失败
# =============================================================================
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$server = Join-Path $root 'QtQQ_Server\build-msvc64\release\QtQQ_Server.exe'
$botExe = Join-Path $root 'QtQQ_Bot\build-x64\release\QtQQ_Bot.exe'
$client = Join-Path $root 'QT_QQ\Debug\QT_QQ.exe'
$mysql = 'C:\Program Files\MySQL\MySQL Server 8.0\bin\mysql.exe'
$dbArgs = @('-uroot', '-padmin123', '-N', '-e')
$script:pass = 0
$script:fail = 0

function Mysql([string]$sql) {
    if (-not (Test-Path $mysql)) { return '' }
    $out = & $mysql @dbArgs $sql 2>&1
    return (($out | Where-Object { $_ -notmatch '^mysql: \[Warning\]' }) -join "`n").Trim()
}
function Check([string]$name, [bool]$ok) {
    if ($ok) { $script:pass++; Write-Host "  [PASS] $name" -ForegroundColor Green }
    else     { $script:fail++; Write-Host "  [FAIL] $name" -ForegroundColor Red }
}

function Wait-Port([int]$port, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) { return $true }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# 给的是"所有窗口的文本拼接"：一个客户端可能同时开了群窗口和私聊窗口
function Read-Page([int]$port) {
    $pages = Invoke-RestMethod -Uri "http://127.0.0.1:$port/json" -TimeoutSec 5
    $all = ''
    foreach ($pg in $pages) {
        if ($pg.type -ne 'page') { continue }
        $ws = [System.Net.WebSockets.ClientWebSocket]::new()
        $ws.ConnectAsync([Uri]$pg.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait(5000) | Out-Null
        $payload = '{"id":1,"method":"Runtime.evaluate","params":{"expression":"document.body.innerText","returnByValue":true}}'
        $bytes = [Text.Encoding]::UTF8.GetBytes($payload)
        $ws.SendAsync((New-Object ArraySegment[byte] -ArgumentList (, $bytes)),
                      [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
                      [Threading.CancellationToken]::None).Wait(5000) | Out-Null
        $buf = New-Object byte[] 262144; $sb = New-Object Text.StringBuilder
        do {
            $res = $ws.ReceiveAsync((New-Object ArraySegment[byte] -ArgumentList (, $buf)),
                                    [Threading.CancellationToken]::None)
            $res.Wait(5000) | Out-Null
            [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $res.Result.Count))
        } while (-not $res.Result.EndOfMessage)
        $all += "`n" + (($sb.ToString() | ConvertFrom-Json).result.result.value)
    }
    return $all
}

function Send-Line($stream, [string]$json) {
    $b = [Text.Encoding]::UTF8.GetBytes($json + "`n")
    $stream.Write($b, 0, $b.Length); $stream.Flush()
}

Write-Host "==== 准备：停掉旧实例、清空消息表 ===="
& (Join-Path $PSScriptRoot 'stop-all.bat') | Out-Null
Start-Sleep -Seconds 2
Mysql "DELETE FROM qt_qq_main.tab_messages;" | Out-Null

Write-Host "==== 启动服务端 / 机器人 ===="
Start-Process -FilePath $server -WorkingDirectory (Split-Path $server) -WindowStyle Minimized
Check '服务端监听 57777' (Wait-Port 57777 15)

$botLog = Join-Path (Split-Path $botExe) 'bot.log'
Remove-Item $botLog -Force -ErrorAction SilentlyContinue
Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', "QtQQ_Bot.exe > `"$botLog`" 2>&1" `
    -WorkingDirectory (Split-Path $botExe) -WindowStyle Hidden
Start-Sleep -Seconds 3

Write-Host "==== 场景 1：10001 给 2652344414 发私聊（对方离线） ===="
$tc = New-Object System.Net.Sockets.TcpClient; $tc.Connect('127.0.0.1', 57777); $st = $tc.GetStream()
Send-Line $st '{"what":"hello","id":"10001"}'
Start-Sleep -Milliseconds 300
Send-Line $st '{"what":"msg","send":"10001","recv":"2652344414","type":1,"msg":[[{"txt":"离线私聊E2E"}]]}'
Start-Sleep -Milliseconds 500
Send-Line $st '{"what":"msg","send":"10001","recv":"2000","type":0,"msg":[[{"txt":"群聊历史E2E"}]]}'
Start-Sleep -Seconds 1
$tc.Close()

$rows = Mysql "SELECT COUNT(*) FROM qt_qq_main.tab_messages;"
Check '消息已落库（2 条）' ([int]$rows -eq 2)

Write-Host "==== 场景 2：2652344414 上线，应自动收到离线私聊 + 群历史 ===="
$env:QTWEBENGINE_REMOTE_DEBUGGING = '9222'
Remove-Item (Join-Path (Split-Path $client) 'e2e_a.log') -Force -ErrorAction SilentlyContinue
$a = Start-Process -FilePath $client `
    -ArgumentList '--autotest', '--account', '2652344414', '--password', 'wsadjkhuang1', '--talk', '2000' `
    -WorkingDirectory (Split-Path $client) -PassThru `
    -RedirectStandardError (Join-Path (Split-Path $client) 'e2e_a.log')
Start-Sleep -Seconds 16

$text = Read-Page 9222
Check '离线私聊自动开窗并显示' ($text -like '*离线私聊E2E*')
Check '群聊历史在打开窗口时回填' ($text -like '*群聊历史E2E*')

$delivered = Mysql "SELECT delivered FROM qt_qq_main.tab_messages WHERE text='离线私聊E2E';"
Check '离线消息标记为已投递' ([int]$delivered -eq 1)

Write-Host "==== 场景 3：AI 私聊回答 ===="
$tc2 = New-Object System.Net.Sockets.TcpClient; $tc2.Connect('127.0.0.1', 57777); $st2 = $tc2.GetStream()
Send-Line $st2 '{"what":"hello","id":"2652344414"}'
Start-Sleep -Milliseconds 300
Send-Line $st2 '{"what":"msg","send":"2652344414","recv":"10010","type":1,"msg":[[{"txt":"你好"}]]}'
$st2.ReadTimeout = 20000
$buf = New-Object byte[] 65536; $got = ''
try {
    while ($true) {
        $n = $st2.Read($buf, 0, $buf.Length)
        if ($n -le 0) { break }
        $got += [Text.Encoding]::UTF8.GetString($buf, 0, $n)
        if ($got -match '"done":true') { break }
    }
} catch {}
$tc2.Close()
Check 'AI 私聊应答（收到 msg_stream）' ($got -match 'msg_stream')

Write-Host "==== 场景 4：服务端重启后客户端自动重连 ===="
Get-Process QtQQ_Server -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 3
Start-Process -FilePath $server -WorkingDirectory (Split-Path $server) -WindowStyle Minimized
Check '服务端重新监听' (Wait-Port 57777 15)
Start-Sleep -Seconds 8     # 客户端 5 秒心跳

$tc3 = New-Object System.Net.Sockets.TcpClient; $tc3.Connect('127.0.0.1', 57777); $st3 = $tc3.GetStream()
Send-Line $st3 '{"what":"hello","id":"10001"}'
Start-Sleep -Milliseconds 300
Send-Line $st3 '{"what":"msg","send":"10001","recv":"2000","type":0,"msg":[[{"txt":"重连后消息E2E"}]]}'
Start-Sleep -Seconds 2
$tc3.Close()

$text2 = Read-Page 9222
Check '服务端重启后客户端不用发消息也恢复接收' ($text2 -like '*重连后消息E2E*')

Write-Host "==== 清理 ===="
Get-Process -Id $a.Id -ErrorAction SilentlyContinue | Stop-Process -Force
& (Join-Path $PSScriptRoot 'stop-all.bat') | Out-Null

Write-Host ""
if ($script:fail -eq 0) {
    Write-Host "全部通过：$($script:pass)/$($script:pass)" -ForegroundColor Green
    exit 0
} else {
    Write-Host "通过 $($script:pass)，失败 $($script:fail)" -ForegroundColor Red
    exit 1
}
