@echo off
rem  stop-all.bat — 关掉三个进程（QtWebEngineProcess 会跟着客户端一起退）
chcp 65001 >nul
taskkill /IM QT_QQ.exe               /F >nul 2>&1
taskkill /IM QtWebEngineProcessd.exe /F >nul 2>&1
taskkill /IM QtQQ_Bot.exe            /F >nul 2>&1
taskkill /IM QtQQ_Server.exe         /F >nul 2>&1
echo 已停止：QT_QQ / QtQQ_Bot / QtQQ_Server
