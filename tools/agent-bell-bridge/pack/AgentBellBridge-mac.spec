# -*- mode: python ; coding: utf-8 -*-
# AgentBellBridge-mac.spec — macOS 打包配置（必须在 Mac 上跑 PyInstaller，不能交叉编译）
#
# 打包（在 Mac 上）：见 pack/build_mac.sh，或手动：
#   python3 -m PyInstaller pack/AgentBellBridge-mac.spec --noconfirm \
#           --distpath pack/dist-mac --workpath pack/build-mac
# 产物：pack/dist-mac/AgentBell Bridge.app
#
# 关键点：
#  · pywebview 的 cocoa 后端 / pystray 的 darwin 后端都是运行时动态导入 → hiddenimports
#  · ui/ 目录随包（运行时 sys._MEIPASS 定位）
#  · BUNDLE 出 .app；图标 agentbell.icns（make_icns.py 预生成，仓库里已带）
import os

HERE = os.path.dirname(os.path.abspath(SPEC))          # pack/
SRC = os.path.dirname(HERE)                            # tools/agent-bell-bridge/

a = Analysis(
    [os.path.join(SRC, "bridge.py")],
    pathex=[SRC],
    binaries=[],
    datas=[
        (os.path.join(SRC, "ui"), "ui"),               # 设置面板 HTML
    ],
    hiddenimports=[
        "webview.platforms.cocoa",                     # pywebview macOS 后端
        "pystray._darwin",                             # 菜单栏图标后端
        "PIL.ImageDraw",
    ],
    excludes=["tkinter", "winrt", "matplotlib", "numpy", "scipy", "pandas"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="AgentBellBridge",
    console=False,
    disable_windowed_traceback=False,
    upx=False,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    name="AgentBellBridge",
    upx=False,
)
app = BUNDLE(
    coll,
    name="AgentBell Bridge.app",
    icon=os.path.join(HERE, "agentbell.icns"),
    bundle_identifier="com.agentbell.bridge",
    info_plist={
        "NSHighResolutionCapable": True,
        "LSMinimumSystemVersion": "12.0",
        "CFBundleShortVersionString": "2.0.0",
        "NSHumanReadableCopyright": "AgentBell",
    },
)
