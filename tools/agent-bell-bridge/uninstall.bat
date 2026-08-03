@echo off
rem ============================================================
rem  AgentBell Bridge 卸载：结束进程 + 删除自启注册
rem  （不删配置 %APPDATA%\AgentBell，想删可手动删那个文件夹）
rem ============================================================
chcp 65001 >nul
echo 移除开机自启……
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v AgentBellBridge /f >nul 2>nul

echo 结束正在运行的实例（如有）……
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"name='python.exe' or name='pythonw.exe'\" | Where-Object { $_.CommandLine -like '*bridge.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" >nul 2>nul

echo 已卸载。配置保留在 %APPDATA%\AgentBell（可手动删除）。
pause
