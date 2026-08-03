#!/bin/bash
# build_mac.sh — 在 Mac 上一键构建 AgentBell Bridge 安装包（.app + 拖装式 .dmg）
#
# 用法：把整个仓库拷到 Mac（或 git clone），然后
#   cd tools/agent-bell-bridge && bash pack/build_mac.sh [applesilicon|intel]
# 产物：pack/Output/AgentBellBridge-mac-<架构>.dmg
# （不带参数时按本机架构命名；GitHub Actions 会分别在两种机器上各构建一份）
#
# 要求：macOS 12+，已装 Python 3.9–3.13（python.org 版或 brew 版均可）。
set -euo pipefail
cd "$(dirname "$0")/.."      # 到 tools/agent-bell-bridge/

case "${1:-$(uname -m)}" in
  arm64|applesilicon) TAG=applesilicon ;;
  x86_64|intel)       TAG=intel ;;
  *)                  TAG="${1}" ;;
esac

echo "[1/4] 安装依赖……"
python3 -m pip install --quiet --upgrade pyinstaller pywebview pystray pillow

echo "[2/4] 生成图标（仓库已带 agentbell.icns 则跳过）……"
[ -f pack/agentbell.icns ] || python3 pack/make_icns.py

echo "[3/4] PyInstaller 出 .app ……"
python3 -m PyInstaller pack/AgentBellBridge-mac.spec --noconfirm \
        --distpath pack/dist-mac --workpath pack/build-mac

echo "[4/4] hdiutil 出拖装式 DMG（含 Applications 快捷方式）……"
STAGE="pack/dist-mac/dmg-stage"
rm -rf "$STAGE" && mkdir -p "$STAGE" pack/Output
cp -R "pack/dist-mac/AgentBell Bridge.app" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
DMG="pack/Output/AgentBellBridge-mac-${TAG}.dmg"
rm -f "$DMG"
hdiutil create -volname "AgentBell Bridge" -srcfolder "$STAGE" \
        -ov -format UDZO "$DMG"
rm -rf "$STAGE"

echo
echo "完成：$DMG"
echo
echo "安装（目标 Mac 上）："
echo "  1. 打开 DMG，把 “AgentBell Bridge.app” 拖到旁边的 Applications 上"
echo "  2. 首次启动：右键 App → 打开（未签名，直接双击会被 Gatekeeper 拦）"
echo "  3. 授权：系统设置 > 隐私与安全性 > 完全磁盘访问权限 → 加入并勾选"
echo "     AgentBell Bridge（读通知中心数据库必需），然后重启该 App"
echo "  4. 开机自启在 App 设置面板里打开（写 ~/Library/LaunchAgents）"
