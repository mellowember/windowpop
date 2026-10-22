// After Effects wrapper for the WindowPop core.
//
// Built against the Adobe After Effects SDK (Examples/Headers, Examples/Util).
// Classic (non-SmartFX) render path: 8 and 16 bpc, Multi-Frame Rendering safe
// because the core is stateless and nothing here is cached between calls.
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "String_Utils.h"
#include "entry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "../core/WindowPop.h"

#define WP_MAJOR_VERSION 0
#define WP_MINOR_VERSION 1
#define WP_BUG_VERSION 0
#define WP_STAGE_VERSION PF_Stage_DEVELOP
#define WP_BUILD_VERSION 1

static_assert(sizeof(wp::Px8) == sizeof(PF_Pixel8), "pixel layout");
static_assert(sizeof(wp::Px16) == sizeof(PF_Pixel16), "pixel layout");

enum {
    WP_INPUT = 0,
    WP_LOOK,
    WP_AMOUNT,
    WP_TRAIL,
    WP_SIZE,
    WP_SEED,
    WP_FOCUS_MODE,
    WP_FOCUS_POINT,
    WP_STYLE,
    WP_UI_SCALE,
    WP_ENTRANCE,
    WP_BACKGROUND,
    WP_DIM,
    WP_BG_COLOR,
    WP_NUM_PARAMS
};

// Stable disk ids: never renumber these once projects exist.
enum {
    ID_LOOK = 1,
    ID_AMOUNT,
    ID_TRAIL,
    ID_SIZE,
    ID_SEED,
    ID_FOCUS_MODE,
    ID_FOCUS_POINT,
    ID_STYLE,
    ID_UI_SCALE,
    ID_ENTRANCE,
    ID_BACKGROUND,
    ID_DIM,
    ID_BG_COLOR
};

static PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    std::snprintf(out_data->return_msg, sizeof(out_data->return_msg), "%s v%d.%d\r%s", "WindowPop",
                  WP_MAJOR_VERSION, WP_MINOR_VERSION, "Pops retro desktop windows over your footage.");
    return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    out_data->my_version =
        PF_VERSION(WP_MAJOR_VERSION, WP_MINOR_VERSION, WP_BUG_VERSION, WP_STAGE_VERSION, WP_BUILD_VERSION);

    // Must match AE_Effect_Global_OutFlags / _2 in WindowPopPiPL.r.
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE |  // 16 bpc
                          PF_OutFlag_WIDE_TIME_INPUT |   // we read the layer at earlier times
                          PF_OutFlag_NON_PARAM_VARY;     // output changes over time with static params
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Look", 2, 1, "Motion Windows|Time Trail", ID_LOOK);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Amount (windows/sec)", 0, 120, 0, 30, 8, PF_Precision_TENTHS, 0, 0, ID_AMOUNT);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Trail Length (sec)", 0.05, 20, 0.1, 5, 1.5, PF_Precision_HUNDREDTHS, 0, 0, ID_TRAIL);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Window Size", 2, 100, 5, 80, 30, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0,
                         ID_SIZE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Random Seed", 0, 100000, 0, 1000, 1, ID_SEED);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Focus", 2, 2, "Off|Focus Point", ID_FOCUS_MODE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Focus Point", 50, 50, false, ID_FOCUS_POINT);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Window Style", 4, 1, "Glossy 2000s|Classic 90s|Modern Dark (Mac)|Modern Light (Mac)", ID_STYLE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Interface Scale", 10, 400, 25, 200, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT,
                         0, ID_UI_SCALE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Entrance", 3, 1, "Pop In|Fade In|None", ID_ENTRANCE);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Background", 4, 2, "Original|Dimmed|Solid Color|Transparent", ID_BACKGROUND);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dim Amount", 0, 100, 0, 100, 60, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0,
                         ID_DIM);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Background Color", 10, 10, 16, ID_BG_COLOR);

    out_data->num_params = WP_NUM_PARAMS;
    return err;
}

static wp::Settings ReadSettings(PF_InData* in_data, PF_ParamDef* params[], const PF_LayerDef* output) {
    wp::Settings s;
    s.look = static_cast<wp::Look>(params[WP_LOOK]->u.pd.value - 1);
    s.windowsPerSecond = params[WP_AMOUNT]->u.fs_d.value;
    s.lifetime = params[WP_TRAIL]->u.fs_d.value;
    s.windowSize = params[WP_SIZE]->u.fs_d.value / 100.0;
    s.seed = static_cast<uint32_t>(params[WP_SEED]->u.sd.value);
    s.useFocus = params[WP_FOCUS_MODE]->u.pd.value == 2;
    // Point values arrive in the current (possibly downsampled) layer space.
    // TODO(verify on device): confirm normalisation at half/quarter preview resolution.
    s.focusX = FIX_2_FLOAT(params[WP_FOCUS_POINT]->u.td.x_value) / std::max<A_long>(1, output->width);
    s.focusY = FIX_2_FLOAT(params[WP_FOCUS_POINT]->u.td.y_value) / std::max<A_long>(1, output->height);
    s.style = static_cast<wp::Style>(params[WP_STYLE]->u.pd.value - 1);
    s.interfaceScale = params[WP_UI_SCALE]->u.fs_d.value / 100.0;
    s.entrance = static_cast<wp::Entrance>(params[WP_ENTRANCE]->u.pd.value - 1);
    s.background = static_cast<wp::Background>(params[WP_BACKGROUND]->u.pd.value - 1);
    s.dimAmount = params[WP_DIM]->u.fs_d.value / 100.0;
    const PF_Pixel& c = params[WP_BG_COLOR]->u.cd.value;
    s.solidColor[0] = c.red / 255.0f;
    s.solidColor[1] = c.green / 255.0f;
    s.solidColor[2] = c.blue / 255.0f;
    return s;
}

template <typename P, typename AEPixel>
static wp::Image<P> Wrap(const PF_EffectWorld& w) {
    wp::Image<P> img;
    img.data = reinterpret_cast<P*>(w.data);
    img.width = w.width;
    img.height = w.height;
    img.rowBytes = w.rowbytes;
    return img;
}

static PF_Err Render(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    PF_Err err = PF_Err_NONE;
    const wp::Settings s = ReadSettings(in_data, params, output);

    // Layer time in seconds. Spawn times are snapped to frame boundaries so a
    // window freezes on a real frame.
    const double tScale = static_cast<double>(in_data->time_scale);
    const double t = in_data->current_time / tScale;
    const std::vector<wp::Window> windows = activeWindows(s, t, output->width, output->height);

    // Check out the input layer once per distinct frozen time.
    std::map<A_long, PF_ParamDef> checked;
    std::vector<A_long> windowTime(windows.size());
    for (size_t i = 0; i < windows.size() && !err; ++i) {
        const A_long step = std::max<A_long>(1, in_data->time_step);
        const A_long frozen = static_cast<A_long>(std::floor(windows[i].spawnTime * tScale / step)) * step;
        windowTime[i] = frozen;
        if (frozen == in_data->current_time || checked.count(frozen)) continue;
        PF_ParamDef def;
        AEFX_CLR_STRUCT(def);
        err = PF_CHECKOUT_PARAM(in_data, WP_INPUT, frozen, in_data->time_step, in_data->time_scale, &def);
        if (!err) checked[frozen] = def;
    }

    PF_EffectWorld* input = &params[WP_INPUT]->u.ld;
    auto frameWorld = [&](size_t i) -> PF_EffectWorld* {
        auto it = checked.find(windowTime[i]);
        if (it == checked.end()) return input;
        PF_EffectWorld* w = &it->second.u.ld;
        // Before the layer starts, or a size mismatch: fall back to the current frame.
        if (!w->data || w->width != output->width || w->height != output->height) return input;
        return w;
    };

    if (!err) {
        if (PF_WORLD_IS_DEEP(output)) {
            std::vector<wp::Image<wp::Px16>> imgs(windows.size());
            std::vector<const wp::Image<wp::Px16>*> frames(windows.size());
            for (size_t i = 0; i < windows.size(); ++i) {
                imgs[i] = Wrap<wp::Px16, PF_Pixel16>(*frameWorld(i));
                frames[i] = &imgs[i];
            }
            const wp::Image<wp::Px16> cur = Wrap<wp::Px16, PF_Pixel16>(*input);
            wp::Image<wp::Px16> out = Wrap<wp::Px16, PF_Pixel16>(*output);
            wp::render(s, windows, frames, cur, out);
        } else {
            std::vector<wp::Image<wp::Px8>> imgs(windows.size());
            std::vector<const wp::Image<wp::Px8>*> frames(windows.size());
            for (size_t i = 0; i < windows.size(); ++i) {
                imgs[i] = Wrap<wp::Px8, PF_Pixel8>(*frameWorld(i));
                frames[i] = &imgs[i];
            }
            const wp::Image<wp::Px8> cur = Wrap<wp::Px8, PF_Pixel8>(*input);
            wp::Image<wp::Px8> out = Wrap<wp::Px8, PF_Pixel8>(*output);
            wp::render(s, windows, frames, cur, out);
        }
    }

    for (auto& kv : checked) {
        PF_Err err2 = PF_CHECKIN_PARAM(in_data, &kv.second);
        if (!err) err = err2;
    }
    return err;
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite* inSPBasicSuitePtr, const char* inHostName,
                                                     const char* inHostVersion) {
    PF_Err result = PF_Err_INVALID_CALLBACK;
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr,
                                     "WindowPop",              // Name
                                     "COM WindowPop",          // Match name, never change
                                     "Stylize",                // Category
                                     AE_RESERVED_INFO,
                                     "EffectMain",             // Entry point
                                     "https://example.com");   // Support URL
    return result;
}

extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output,
                  void* extra) {
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT: err = About(in_data, out_data, params, output); break;
            case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(in_data, out_data, params, output); break;
            case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data, params, output); break;
            case PF_Cmd_RENDER: err = Render(in_data, out_data, params, output); break;
            default: break;
        }
    } catch (PF_Err& thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_OUT_OF_MEMORY;
    }
    return err;
}
