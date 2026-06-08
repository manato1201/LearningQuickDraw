"""
shelf_tool.py
Houdini シェルフツール スクリプト。

Houdini シェルフ → 新規ツール作成 → Script タブにこの内容を貼り付け。
"""

import hou
import os

# 選択されたノード、またはルートに作成
parent = hou.ui.paneTabOfType(hou.paneTabType.NetworkEditor).pwd()
if parent.childTypeCategory() != hou.objNodeTypeCategory():
    parent = hou.node("/obj")

# GEO ネットワーク作成
geo = parent.createNode("geo", "JetFluidSim")

# HDA インスタンスを作成
hda_path = r"C:/Users/matuu/Desktop/GameDevelopment/Learners/houdini-fluid-hda/jet_fluid_loader.hda"

if not hou.hda.isInstalled(hda_path):
    hou.hda.installFile(hda_path)

loader = geo.createNode("jet_fluid_loader", "loader")
loader.moveToGoodPosition()

# Null 出力ノードを追加
null = geo.createNode("null", "OUT")
null.setInput(0, loader)
null.moveToGoodPosition()
null.setDisplayFlag(True)
null.setRenderFlag(True)

geo.layoutChildren()

# UI でパラメーターを開く
loader.openTypePropertiesDialog()

hou.ui.displayMessage(
    "JetFluidLoader created!\n\n"
    "1. Set Cache Directory to your .vol folder\n"
    "2. Play the timeline\n"
    "3. Check the node comment for performance stats",
    title="Jet Fluid Loader"
)
