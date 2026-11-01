@echo off
rem ===========================================================================
rem  build-all.bat — 依次编译三个工程（全部走 MSBuild / Qt VS Tools）
rem
rem    QtQQ_Server   x64   QtQQ_Server.vcxproj
rem    QtQQ_Bot      x64   QtQQ_Bot.vcxproj
rem    QT_QQ         x86   QT_QQ.vcxproj
rem
rem  不需要先开 VS 开发者命令行：这里自己用 vswhere 找 MSBuild。
rem ===========================================================================
setlocal
chcp 65001 >nul
set "ROOT=%~dp0.."

rem Qt VS Tools 靠 QtMsBuild 环境变量定位 moc/uic/rcc（用哪个套件由 vcxproj 决定）
set "QtMsBuild=%LOCALAPPDATA%\QtMsBuild"

rem vswhere 找 MSBuild（装了 VS 的机器上一定有）
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [错误] 找不到 vswhere.exe，请先安装 Visual Studio
    pause & exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo [错误] 没找到 MSBuild，请确认安装了"使用 C++ 的桌面开发"工作负载
    pause & exit /b 1
)

echo  使用: %MSBUILD%
echo.

echo ==== [1/3] QtQQ_Server（x64） ====
"%MSBUILD%" "%ROOT%\QtQQ_Server\QtQQ_Server.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo || (pause & exit /b 1)

echo ==== [2/3] QtQQ_Bot（x64） ====
"%MSBUILD%" "%ROOT%\QtQQ_Bot\QtQQ_Bot.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64 /m /v:m /nologo || (pause & exit /b 1)

echo ==== [3/3] QT_QQ（x86） ====
"%MSBUILD%" "%ROOT%\QT_QQ\QT_QQ.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=Win32 /m /v:m /nologo || (pause & exit /b 1)

echo.
echo 三个工程都编译完成。
pause
