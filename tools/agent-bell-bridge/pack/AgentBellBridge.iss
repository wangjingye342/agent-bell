; AgentBellBridge.iss — Inno Setup 安装器脚本
; 编译：iscc pack\AgentBellBridge.iss   （需先跑 PyInstaller 出 pack/dist/AgentBellBridge/）
; 产物：pack/Output/AgentBellBridge-Setup.exe
; 特点：单用户安装（不要管理员）、可选开机自启、卸载时清理自启注册表项

#define AppName "AgentBell Bridge"
#define AppVersion "2.2.1"
#define AppExe "AgentBellBridge.exe"

[Setup]
AppId={{7E3A9C41-5B8D-4F2E-9C6A-AGENTBELL01}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=AgentBell
DefaultDirName={userpf}\AgentBellBridge
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=AgentBellBridge-Setup
OutputDir=Output
Compression=lzma2
SolidCompression=yes
SetupIconFile=agentbell.ico
UninstallDisplayIcon={app}\{#AppExe}
WizardStyle=modern
ShowLanguageDialog=no

[Languages]
; 语言文件随仓库带（pack/ChineseSimplified.isl），不依赖 Inno 安装了中文语言包
Name: "chs"; MessagesFile: "ChineseSimplified.isl"

[Tasks]
Name: "autostart"; Description: "开机自动启动（推荐，装完就不用管了）"
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked

[Files]
Source: "dist\AgentBellBridge\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{userprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; 开机自启（HKCU Run，与程序内「开机自动启动」开关是同一个键，互相兼容）
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "AgentBellBridge"; ValueData: """{app}\{#AppExe}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "立即启动 {#AppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; 卸载前先结束正在运行的实例（托盘常驻杀不掉会导致文件占用）
Filename: "taskkill"; Parameters: "/f /im {#AppExe}"; Flags: runhidden skipifdoesntexist; \
    RunOnceId: "KillBridge"

[UninstallDelete]
; 日志/配置在 %APPDATA%\AgentBell，留着（重装还能用缓存的设备 IP）；只清程序目录
Type: filesandordirs; Name: "{app}"
