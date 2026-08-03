@echo off
rem ============================================================
rem  AgentBell Bridge 一键安装：装依赖 + 注册开机自启 + 立即启动
rem ============================================================
setlocal
cd /d "%~dp0"
chcp 65001 >nul

echo [1/3] 安装 Python 依赖……
python -m pip install --quiet pystray pillow pywebview winrt-runtime "winrt-Windows.Foundation" "winrt-Windows.Foundation.Collections" "winrt-Windows.UI.Notifications" "winrt-Windows.UI.Notifications.Management" "winrt-Windows.ApplicationModel"
if errorlevel 1 (
    echo   失败：请确认已安装 Python 3.9-3.14 且 python 在 PATH 上。
    pause
    exit /b 1
)

echo [2/3] 注册开机自启（当前用户，无需管理员）……
for /f "delims=" %%i in ('python -c "import sys,os;p=os.path.join(os.path.dirname(sys.executable),'pythonw.exe');print(p if os.path.exists(p) else sys.executable)"') do set PYW=%%i
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v AgentBellBridge /t REG_SZ /d "\"%PYW%\" \"%~dp0bridge.py\"" /f >nul

echo [3/3] 启动 AgentBell Bridge……
start "" "%PYW%" "%~dp0bridge.py"

echo.
echo 完成！托盘里出现铃铛图标即在运行（绿点=设备在线，红点=离线）。
echo 首次使用请确认 Windows 已允许应用访问通知：
echo   设置 ^> 隐私和安全性 ^> 通知 ^> 让应用访问通知
echo （托盘菜单「打开设置」里有直达按钮）
pause
