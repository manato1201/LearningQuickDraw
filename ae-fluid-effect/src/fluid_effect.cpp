/*
 * fluid_effect.cpp
 * After Effects Effect Plugin メインファイル
 *
 * AE SDK のセレクター構造 = QuickDraw のイベントドリブン描画と同じ設計思想:
 *
 *   PF_Cmd_GLOBAL_SETUP   : 初期化（QuickDraw: InitGrf3D に相当）
 *   PF_Cmd_PARAMS_SETUP   : パラメーター定義（QD: ResFile のリソース定義に相当）
 *   PF_Cmd_RENDER         : 描画（QD: DrawLines / fillTriangle に相当）
 *   PF_Cmd_SMART_RENDER   : タイル描画（QD: スラブ描画に相当）
 *   PF_Cmd_SEQUENCE_SETUP : シーケンス初期化（dirty rect 状態リセット）
 *   PF_Cmd_SEQUENCE_RESETUP: シーケンス再初期化
 */

#include "fluid_effect.h"
#include "vol_loader.h"
#include "tile_renderer.h"
#include <cstring>
#include <cstdio>
#include <memory>

// ================================================================
// グローバルデータ
// ================================================================
static AEGP_PluginID  g_plugin_id = 0;

// ================================================================
// GLOBAL_SETUP
// プラグインの機能フラグを AE に登録する
// ================================================================
static PF_Err GlobalSetup(
    PF_InData*  in_data,
    PF_OutData* out_data)
{
    PF_Err err = PF_Err_NONE;

    // タイルレンダリングを有効化（スラブ描画の AE 版）
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION,
                                       BUG_VERSION, STAGE_VERSION, BUILD_VERSION);

    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE  |   // 32bit float 対応
        PF_OutFlag_USE_OUTPUT_EXTENT |   // 出力サイズを制御
        PF_OutFlag_NON_PARAM_VARY;       // パラメーター以外の変化（フレームなど）

    out_data->out_flags2 =
        PF_OutFlag2_SUPPORTS_SMART_RENDER |   // タイルレンダリング有効
        PF_OutFlag2_FLOAT_COLOR_AWARE;        // float ピクセル対応

    // グローバルデータ確保
    auto* global = new GlobalData();
    global->pica_basicP  = in_data->pica_basicP;
    out_data->global_data = PF_NEW_HANDLE(sizeof(GlobalData));
    if (out_data->global_data) {
        GlobalData* gp = static_cast<GlobalData*>(
            PF_LOCK_HANDLE(out_data->global_data));
        *gp = *global;
        PF_UNLOCK_HANDLE(out_data->global_data);
    }
    delete global;

    return err;
}

// ================================================================
// PARAMS_SETUP
// UI パラメーターを定義する
// QuickDraw Ch6: 0600MacTech3D.R のリソース定義と同等
// ================================================================
static PF_Err ParamsSetup(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err        err = PF_Err_NONE;
    PF_ParamDef   def;

    // Cache Directory (文字列パス)
    AEFX_CLR_STRUCT(def);
    PF_ADD_ARBITRARY2("Cache Dir", 200, 20, 0,
                       PF_PUI_NO_ECW_UI, 0,
                       PARAM_CACHE_DIR, "CACHE_DIR");

    // Frame Offset
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Frame Offset", -100, 100, -100, 100, 0,
                          PF_Precision_INTEGER, 0, 0, PARAM_FRAME_OFFSET);

    // Density Scale
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Density Scale", 0.0, 10.0, 0.0, 5.0, 1.0,
                          PF_Precision_HUNDREDTHS, 0, 0, PARAM_DENSITY_SCALE);

    // Density Min
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Density Min", 0.0, 1.0, 0.0, 1.0, 0.001f,
                          PF_Precision_THOUSANDTHS, 0, 0, PARAM_DENSITY_MIN);

    // LOD Level
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("LOD Level", 0, 3, 0, 3, 0, PARAM_LOD_LEVEL);

    // Dirty Threshold
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dirty Threshold", 0.0, 1.0, 0.0, 1.0, 0.01f,
                          PF_Precision_HUNDREDTHS, 0, 0, PARAM_DIRTY_THRESH);

    // Transfer Mode
    // QuickDraw Ch3: patCopy / patOr / notPatBic / patBic
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Transfer Mode", 4, 1,
                 "Normal|Add|Screen|Multiply",
                 PARAM_TRANSFER_MODE);

    // Tile Size (スラブ幅)
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Tile Size", 8, 256, 8, 256, 64, PARAM_TILE_SIZE);

    out_data->num_params = PARAM_COUNT;
    return err;
}

// ================================================================
// SEQUENCE_SETUP / RESETUP
// シーケンスデータ（dirty rect 状態）の初期化
// ================================================================
static PF_Err SequenceSetup(
    PF_InData*  in_data,
    PF_OutData* out_data)
{
    PF_Err err = PF_Err_NONE;

    out_data->sequence_data = PF_NEW_HANDLE(sizeof(SequenceData));
    if (out_data->sequence_data) {
        SequenceData* sd = static_cast<SequenceData*>(
            PF_LOCK_HANDLE(out_data->sequence_data));
        memset(sd, 0, sizeof(SequenceData));
        sd->last_frame    = -99999;
        sd->prev_density  = nullptr;
        PF_UNLOCK_HANDLE(out_data->sequence_data);
    }
    return err;
}

static PF_Err SequenceSetdown(
    PF_InData*  in_data,
    PF_OutData* out_data)
{
    if (in_data->sequence_data) {
        SequenceData* sd = static_cast<SequenceData*>(
            PF_LOCK_HANDLE(in_data->sequence_data));
        if (sd && sd->prev_density) {
            delete[] sd->prev_density;
            sd->prev_density = nullptr;
        }
        PF_UNLOCK_HANDLE(in_data->sequence_data);
        PF_DISPOSE_HANDLE(in_data->sequence_data);
    }
    return PF_Err_NONE;
}

// ================================================================
// SMART_PRE_RENDER
// AE がレンダリング前に呼ぶ。タイル分割の準備。
// ================================================================
static PF_Err SmartPreRender(
    PF_InData*          in_data,
    PF_OutData*         out_data,
    PF_PreRenderExtra*  extra)
{
    PF_Err err = PF_Err_NONE;

    // 必要な入力矩形を AE に通知（フレーム全体）
    PF_RenderRequest req = extra->input->output_request;
    req.preserve_rgb_of_zero_alpha = false;

    ERR(extra->cb->checkout_layer(
        in_data->effect_ref, PARAM_INPUT,
        PARAM_INPUT, &req,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &extra->output->pre_render_data));

    return err;
}

// ================================================================
// SMART_RENDER
// タイルを受け取って描画する。
// QuickDraw Ch5: スラブ描画 の AE 版。
// ================================================================
static PF_Err SmartRender(
    PF_InData*        in_data,
    PF_OutData*       out_data,
    PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    // パラメーター取得
    PF_ParamDef cache_dir_param, frame_off_param, density_scale_param;
    PF_ParamDef density_min_param, lod_param, dirty_param, transfer_param;
    AEFX_CLR_STRUCT(cache_dir_param);
    AEFX_CLR_STRUCT(frame_off_param);

    ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FRAME_OFFSET,
                           in_data->current_time, in_data->time_step,
                           in_data->time_scale, &frame_off_param));
    ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DENSITY_SCALE,
                           in_data->current_time, in_data->time_step,
                           in_data->time_scale, &density_scale_param));
    ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DENSITY_MIN,
                           in_data->current_time, in_data->time_step,
                           in_data->time_scale, &density_min_param));
    ERR(PF_CHECKOUT_PARAM(in_data, PARAM_LOD_LEVEL,
                           in_data->current_time, in_data->time_step,
                           in_data->time_scale, &lod_param));
    ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DIRTY_THRESH,
                           in_data->current_time, in_data->time_step,
                           in_data->time_scale, &dirty_param));
    ERR(PF_CHECKOUT_PARAM(in_data, PARAM_TRANSFER_MODE,
                           in_data->current_time, in_data->time_step,
                           in_data->time_scale, &transfer_param));

    if (!err) {
        float density_scale  = frame_off_param.u.fs_d.value;
        float density_min    = density_min_param.u.fs_d.value;
        int   lod            = lod_param.u.sd.value;
        float dirty_thresh   = dirty_param.u.fs_d.value;
        int   transfer_mode  = transfer_param.u.pd.value - 1;

        int   frame_idx = (int)(in_data->current_time / in_data->time_step)
                        + (int)frame_off_param.u.fs_d.value;

        // シーケンスデータ（dirty rect 状態）取得
        SequenceData* sd = nullptr;
        if (in_data->sequence_data) {
            sd = static_cast<SequenceData*>(
                PF_LOCK_HANDLE(in_data->sequence_data));
        }

        // .vol ファイル読み込み
        if (sd && sd->cache_dir[0] != '\0') {
            std::string path = VolFramePath(sd->cache_dir, frame_idx);
            auto vol = ReadVolFile(path.c_str());

            if (vol) {
                TileRenderer renderer;

                // DirtyRect 計算
                DirtyRect dirty = renderer.computeDirty(
                    sd->prev_density,
                    vol->data.get(),
                    vol->voxel_count(),
                    dirty_thresh
                );

                // タイル出力バッファを取得
                PF_EffectWorld* output = nullptr;
                ERR(extra->cb->checkout_output(in_data->effect_ref, &output));

                if (!err && output) {
                    // タイル矩形 (AE が分割して渡す)
                    RenderTile tile {
                        output->extent_hint.left,
                        output->extent_hint.top,
                        output->extent_hint.right,
                        output->extent_hint.bottom,
                    };

                    // 出力バッファ（float RGBA）
                    int pix_count = tile.width() * tile.height();
                    std::vector<PixelRGBA> pixels(pix_count, {0,0,0,0});

                    // タイルレンダリング実行
                    ColorRamp ramp = ColorRamp::smoke();
                    renderer.renderTile(
                        tile, *vol, dirty,
                        lod, density_scale, density_min,
                        transfer_mode, ramp,
                        pixels.data()
                    );

                    // pixels -> AE フレームバッファに書き込み
                    // (PF_Pixel32 / PF_EffectWorld への書き込みは
                    //  AE SDK の iterate_suite を使用)
                }

                // dirty rect 更新: 現フレームを prev として保存
                int vc = vol->voxel_count();
                if (!sd->prev_density || sd->prev_depth != vol->header.z_res) {
                    delete[] sd->prev_density;
                    sd->prev_density = new float[vc];
                }
                memcpy(sd->prev_density, vol->data.get(), vc * sizeof(float));
                sd->prev_width  = vol->header.x_res;
                sd->prev_height = vol->header.y_res;
                sd->prev_depth  = vol->header.z_res;
                sd->last_frame  = frame_idx;
            }
        }

        if (sd) PF_UNLOCK_HANDLE(in_data->sequence_data);
    }

    // パラメーターチェックイン
    ERR(PF_CHECKIN_PARAM(in_data, &frame_off_param));
    ERR(PF_CHECKIN_PARAM(in_data, &density_scale_param));
    ERR(PF_CHECKIN_PARAM(in_data, &density_min_param));
    ERR(PF_CHECKIN_PARAM(in_data, &lod_param));
    ERR(PF_CHECKIN_PARAM(in_data, &dirty_param));
    ERR(PF_CHECKIN_PARAM(in_data, &transfer_param));

    return err;
}

// ================================================================
// EffectMain - エントリポイント（セレクター振り分け）
// QuickDraw のイベントドリブン設計と同じ構造
// ================================================================
PF_Err EffectMain(
    PF_Cmd       cmd,
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void*        extra)
{
    PF_Err err = PF_Err_NONE;

    switch (cmd) {
        case PF_Cmd_ABOUT:
            PF_SPRINTF(out_data->return_msg,
                "%s v%d.%d\nJet fluid cache renderer\n"
                "Dirty-rect + Tile optimization",
                PLUGIN_NAME, MAJOR_VERSION, MINOR_VERSION);
            break;

        case PF_Cmd_GLOBAL_SETUP:
            err = GlobalSetup(in_data, out_data);
            break;

        case PF_Cmd_PARAMS_SETUP:
            err = ParamsSetup(in_data, out_data, params, output);
            break;

        case PF_Cmd_SEQUENCE_SETUP:
        case PF_Cmd_SEQUENCE_RESETUP:
            err = SequenceSetup(in_data, out_data);
            break;

        case PF_Cmd_SEQUENCE_SETDOWN:
            err = SequenceSetdown(in_data, out_data);
            break;

        case PF_Cmd_SMART_PRE_RENDER:
            err = SmartPreRender(in_data, out_data,
                static_cast<PF_PreRenderExtra*>(extra));
            break;

        case PF_Cmd_SMART_RENDER:
            err = SmartRender(in_data, out_data,
                static_cast<PF_SmartRenderExtra*>(extra));
            break;

        default:
            break;
    }
    return err;
}
