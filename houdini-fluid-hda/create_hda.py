"""
create_hda.py
Houdini セッション内で実行して HDA を自動生成するスクリプト。

使い方:
  Houdini を起動 → Python Shell (Alt+Shift+P) で以下を実行:
    exec(open(r"C:/Users/matuu/Desktop/GameDevelopment/Learners/houdini-fluid-hda/create_hda.py").read())
"""

import hou
import os

HDA_PATH = r"C:/Users/matuu/Desktop/GameDevelopment/Learners/houdini-fluid-hda/jet_fluid_loader.hda"
PY_DIR   = r"C:/Users/matuu/Desktop/GameDevelopment/Learners/houdini-fluid-hda/python"

# ----------------------------------------------------------------
# 1. Python SOP を作成
# ----------------------------------------------------------------
obj     = hou.node("/obj")
geo_net = obj.createNode("geo", "jet_fluid_setup")
sop     = geo_net.createNode("python", "JetFluidLoader")

# ----------------------------------------------------------------
# 2. Cook スクリプトを設定
# ----------------------------------------------------------------
cook_code = f"""
import sys
sys.path.insert(0, r"{PY_DIR}")
from sop_cook import cook
cook(hou.pwd(), hou.pwd().geometry())
"""
sop.parm("python").set(cook_code)

# ----------------------------------------------------------------
# 3. パラメーターインターフェースを定義
# ----------------------------------------------------------------
ptg = sop.parmTemplateGroup()

# フォルダ: Cache
cache_folder = hou.FolderParmTemplate("cache_folder", "Cache")
cache_folder.addParmTemplate(
    hou.StringParmTemplate("cache_dir", "Cache Directory", 1,
        string_type=hou.stringParmType.FileReference,
        file_type=hou.fileType.Directory,
        default_value=("",))
)

# フォルダ: Optimization
opt_folder = hou.FolderParmTemplate("opt_folder", "Optimization")
opt_folder.addParmTemplate(
    hou.IntParmTemplate("lod", "LOD Level", 1,
        default_value=(0,), min=0, max=3,
        help="0=Full, 1=Half(1/8 cost), 2=Quarter(1/64 cost)")
)
opt_folder.addParmTemplate(
    hou.FloatParmTemplate("threshold", "Dirty Threshold", 1,
        default_value=(0.01,), min=0.0, max=1.0,
        help="Voxels changing less than this are skipped (QuickDraw dirty-rect)")
)
opt_folder.addParmTemplate(
    hou.FloatParmTemplate("density_min", "Min Density", 1,
        default_value=(0.001,), min=0.0, max=1.0)
)

# フォルダ: Output
out_folder = hou.FolderParmTemplate("out_folder", "Output")
out_folder.addParmTemplate(
    hou.IntParmTemplate("output_mode", "Output Mode", 1,
        default_value=(0,), min=0, max=1,
        help="0=Points, 1=Volume")
)

ptg.append(cache_folder)
ptg.append(opt_folder)
ptg.append(out_folder)
sop.setParmTemplateGroup(ptg)

# ----------------------------------------------------------------
# 4. HDA として保存
# ----------------------------------------------------------------
sop.createDigitalAsset(
    name         = "jet_fluid_loader",
    hda_file_name= HDA_PATH,
    description  = "Jet Fluid Cache Loader (dirty-rect + BVH optimized)",
    min_num_inputs  = 0,
    max_num_inputs  = 0,
)

print(f"HDA created: {HDA_PATH}")
print("Install: Houdini > Assets > Install Asset Library > select .hda")
