@echo off
rem ===========================================================================
rem  start-all.bat — 一键拉起整套 QtQQ（服务端 -> AI 助手 -> 客户端）
rem
rem  顺序不能乱：客户端和机器人都要连服务端的 57777，服务端没起来它们会
rem  连不上（机器人有 5 秒心跳重连，客户端要重开窗口）。
rem
rem  重复启动会出大问题：两个机器人 = 每条消息得到两个回答，两个服务端 =
rem  一个抢到端口、另一个空转。所以这里先检测，已在运行就问你一句。
rem ===========================================================================
setlocal
chcp 65001 >nul

set "ROOT=%~dp0.."
set "SERVER=%ROOT%\QtQQ_Server\build-msvc64\release\QtQQ_Server.exe"
set "BOT=%ROOT%\QtQQ_Bot\build-x64\release\QtQQ_Bot.exe"
set "CLIENT=%ROOT%\QT_QQ\Debug\QT_QQ.exe"

if not exist "%SERVER%" (echo [错误] 找不到服务端: %SERVER% & echo 先跑 scripts\build-all.bat & pause & exit /b 1)
if not exist "%BOT%"    (echo [错误] 找不到机器人: %BOT%    & echo 先跑 scripts\build-all.bat & pause & exit /b 1)
if not exist "%CLIENT%" (echo [错误] 找不到客户端: %CLIENT% & echo 先跑 scripts\build-all.bat & pause & exit /b 1)

rem ---- 已经在跑？先问一句，避免出现两个机器人/两个服务端 ----
set "RUNNING="
for %%I in (QtQQ_Server.exe QtQQ_Bot.exe QT_QQ.exe) do (
    tasklist /FI "IMAGENAME eq %%I" 2>nul | find /I "%%I" >nul && set "RUNNING=1"
)
if defined RUNNING (
    echo [提示] 检测到 QtQQ 已经在运行。
    choice /C YN /N /M "是否先全部停止再重新启动？[Y/N] "
    if errorlevel 2 (
        echo 已取消，保持现状。
        timeout /t 2 >nul
        exit /b 1
    )
    call "%~dp0stop-all.bat"
    timeout /t 2 >nul
)

echo [1/3] 启动服务端...
start "QtQQ_Server" /D "%ROOT%\QtQQ_Server\build-msvc64\release" "%SERVER%"

echo       等待 57777 端口监听...
powershell -NoProfile -Command "$deadline=(Get-Date).AddSeconds(15); while((Get-Date) -lt $deadline){ if(Get-NetTCPConnection -LocalPort 57777 -State Listen -ErrorAction SilentlyContinue){ exit 0 }; Start-Sleep -Milliseconds 300 }; exit 1"
if errorlevel 1 (
    echo [错误] 服务端 15 秒内没有监听 57777，可能端口被占用或数据库连不上
    pause & exit /b 1
)

echo [2/3] 启动 AI 助手（控制台窗口里能看到每条提问和工具调用）...
start "QtQQ_Bot" /D "%ROOT%\QtQQ_Bot\build-x64\release" "%BOT%"
timeout /t 2 >nul

echo [3/3] 启动客户端...
start "QT_QQ" /D "%ROOT%\QT_QQ\Debug" "%CLIENT%"

echo.
echo 全部已启动。停止请运行 scripts\stop-all.bat
timeout /t 3 >nul
