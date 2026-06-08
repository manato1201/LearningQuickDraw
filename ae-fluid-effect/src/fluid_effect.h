/*
 * fluid_effect.h
 * After Effects Fluid Cache Effect Plugin
 *
 * QuickDraw との対応:
 *   PF_EffectWorld         <-> GrafPort portBits (BitMap)
 *   PF_Fixed (16.16)       <-> QuickDraw Ch5: 固定小数点 (0503DDAFixedLineSpeed.p)
 *   PF_Cmd_RENDER          <-> QuickDraw Ch3: DrawLines の描画ループ
 *   PF_Tile                <-> QuickDraw Ch5: スラブ (0507FixedRectSlabDrawLine.p)
 *   Transfer modes         <-> QuickDraw Ch3: patCopy / patOr / patXor (0303TransferModes.p)
 *   Dirty rect             <-> QuickDraw Ch3: RegionClipping (0310RegionClipping.p)
 *
 * AE SDK セットアップ:
 *   1. Adobe After Effects SDK を https://developer.adobe.com からダウンロード
 *   2. SDK の Headers / Resources フォルダを include / resource パスに追加
 *   3. Visual Studio 2022 でビルド → .aex ファイルが生成される
 *   4. .aex を C:/Program Files/Adobe/After Effects/Support Files/Plug-ins/ にコピー
 */

#pragma once

// AE SDK ヘッダー（SDK インストール後に有効になる）
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"

// ----------------------------------------------------------------
// プラグイン識別
// ----------------------------------------------------------------
#define PLUGIN_NAME        "Jet Fluid Effect"
#define PLUGIN_MATCH_NAME  "JET_FLUID_EFFECT"
#define PLUGIN_DESCRIPTION "Fluid cache renderer with dirty-rect and tile optimization"
#define MAJOR_VERSION      1
#define MINOR_VERSION      0
#define BUG_VERSION        0
#define STAGE_VERSION      PF_Stage_DEVELOP
#define BUILD_VERSION      1

// ----------------------------------------------------------------
// パラメーター インデックス
// ----------------------------------------------------------------
enum {
    PARAM_INPUT = 0,       // 入力レイヤー（AE標準）

    PARAM_CACHE_DIR,       // キャッシュディレクトリパス
    PARAM_FRAME_OFFSET,    // フレームオフセット
    PARAM_DENSITY_SCALE,   // 密度スケール
    PARAM_DENSITY_MIN,     // 最低密度閾値
    PARAM_LOD_LEVEL,       // LOD レベル (0-3)
    PARAM_DIRTY_THRESH,    // ダーティレクト閾値
    PARAM_TRANSFER_MODE,   // 合成モード
    PARAM_TILE_SIZE,       // タイルサイズ (QuickDraw スラブ幅)
    PARAM_COLOR_RAMP,      // 密度->色マッピング

    PARAM_COUNT
};

// ----------------------------------------------------------------
// 合成モード（QuickDraw Ch3: 転送モードに対応）
// ----------------------------------------------------------------
enum TransferMode {
    TRANSFER_NORMAL  = 0,  // patCopy  相当
    TRANSFER_ADD     = 1,  // patOr    相当
    TRANSFER_SCREEN  = 2,  // notPatBic 相当
    TRANSFER_MULTIPLY= 3,  // patBic   相当
};

// ----------------------------------------------------------------
// プラグインの大域状態
// ----------------------------------------------------------------
struct GlobalData {
    AEGP_PluginID  plug_id;
    SPBasicSuite*  pica_basicP;
};

// ----------------------------------------------------------------
// シーケンスデータ（コンポジションごとの状態）
// Dirty rect の前フレームデータを保持する
// QuickDraw Ch3: RegionClipping の状態管理と同じ思想
// ----------------------------------------------------------------
struct SequenceData {
    A_char    cache_dir[512];
    A_long    last_frame;
    A_long    dirty_count;    // 変化ピクセル数（統計）
    A_long    total_count;    // 全ピクセル数
    float*    prev_density;   // 前フレームの密度バッファ
    A_long    prev_width;
    A_long    prev_height;
    A_long    prev_depth;
};

// ----------------------------------------------------------------
// エントリポイント宣言
// ----------------------------------------------------------------
extern "C" {
    DllExport PF_Err EffectMain(
        PF_Cmd      cmd,
        PF_InData*  in_data,
        PF_OutData* out_data,
        PF_ParamDef* params[],
        PF_LayerDef* output,
        void*        extra
    );
}
