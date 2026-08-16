# -*- mode: python ; coding: utf-8 -*-
# AgentBellBridge.spec — PyInstaller 打包配置（onedir：目录模式，杀软误报少、启动快）
#
# 打包：cd tools/agent-bell-bridge && python -m PyInstaller pack/AgentBellBridge.spec --noconfirm
# 产物：pack/dist/AgentBellBridge/AgentBellBridge.exe（整个目录就是程序，拷走即用）
#
# 关键坑（都处理了）：
#  · winrt 模块化包全部动态导入 → 必须显式列 hiddenimports，否则打包后监听线程起不来
#  · pystray 的 win32 后端也是动态选择的 → 同样列进去
#  · pywebview 的 winforms/edgechromium 后端同为动态导入；其 .NET DLL 由
#    pyinstaller-hooks-contrib 的 hook-webview 收集（webview/lib/*.dll）
#  · UI 是本地 HTML → datas 带上 ui/ 目录，运行时经 sys._MEIPASS 定位
#  · console=False：托盘程序不开黑窗
import os

HERE = os.path.dirname(os.path.abspath(SPEC))          # pack/
SRC = os.path.dirname(HERE)                            # tools/agent-bell-bridge/

a = Analysis(
    [os.path.join(SRC, "bridge.py")],
    pathex=[SRC],
    binaries=[],
    datas=[
        (os.path.join(SRC, "ui"), "ui"),               # 设置面板 HTML
        (os.path.join(HERE, "icon_art.py"), "pack"),    # 托盘/菜单栏图标画法
    ],
    hiddenimports=[
        # —— WinRT 通知监听（bridge 在线程里 import，PyInstaller 静态分析看不到）——
        "winrt.windows.ui.notifications",
        "winrt.windows.ui.notifications.management",
        "winrt.windows.foundation",
        "winrt.windows.foundation.collections",
        "winrt.windows.applicationmodel",
        "winrt.system",
        # —— 托盘后端（pystray 运行时按平台动态挑）——
        "pystray._win32",
        # —— pywebview Windows 后端（动态 import）——
        "webview.platforms.winforms",
        "webview.platforms.edgechromium",
        "clr_loader",
        # —— PIL 图标绘制 ——
        "PIL.ImageDraw", "PIL.ImageTk",
    ],
    excludes=["matplotlib", "numpy", "scipy", "pandas", "tkinter"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="AgentBellBridge",
    icon=os.path.join(HERE, "agentbell.ico"),
    console=False,                 # 托盘常驻，不要控制台窗口
    disable_windowed_traceback=False,
    upx=False,                     # 不用 UPX：压缩收益小、杀软误报率高
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    name="AgentBellBridge",
    upx=False,
)
