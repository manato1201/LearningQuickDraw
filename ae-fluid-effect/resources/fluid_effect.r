/*
 * fluid_effect.r
 * After Effects プラグインリソース定義
 *
 * QuickDraw Ch6: 0600MacTech3D.R と同じ形式。
 * AE は今もクラシック Mac OS のリソースファイル形式を使用している。
 *
 * ビルド時に Rez コンパイラー（AE SDK付属）で .rsrc に変換される。
 */

#include "AEConfig.h"
#include "AE_EffectVers.h"

#define plugInName      "Jet Fluid Effect"
#define plugInMatchName "JET_FLUID_EFFECT"

resource 'PiPL' (16000) {
    {
        Kind {
            AEEffect
        },
        Name {
            plugInName
        },
        Category {
            "FluidStudy"
        },
        CodeWin64X86 {
            "EffectMain"
        },
        AE_PiPL_Version {
            2, 0
        },
        AE_Effect_Spec_Version {
            PF_PLUG_IN_VERSION,
            PF_PLUG_IN_SUBVERS
        },
        AE_Effect_Version {
            0x00010000      /* 1.0.0 */
        },
        AE_Effect_Info_Flags {
            0
        },
        AE_Effect_Global_InFlags {
            0
        },
        AE_Effect_Global_InFlags2 {
            0
        },
        AE_Effect_Match_Name {
            plugInMatchName
        },
        AE_Reserved_Info {
            8
        }
    }
};
