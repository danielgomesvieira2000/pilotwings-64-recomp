// The host side of the C patches: functions patches call that run as C++.
//
// Each is named in patches/syms.ld at an address between 0x8F000000 and
// 0x90000000, which N64Recomp turns into a direct call with the recompiled
// signature. Arguments arrive in the MIPS O32 registers (a0-a3 are ctx->r4 to
// ctx->r7) and results go back in v0 (ctx->r2) or, for a float, f0.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "recomp.h"

#include <ultramodern/ultramodern.hpp>
#include <ultramodern/config.hpp>

#include "pw64/callbacks.h"

extern "C" {

// Called by the _uvScDoneGfx patch (patches/framerate.c) once per frame the game
// presents. Counting those over a two-second window is the game's frame rate,
// which is reported beside the rate RT64 is presenting at, whenever either
// changes: the first says whether the machine is keeping up with the game, the
// second whether the Framerate setting took. PW64_FRAME_STATS=1 prints every
// window instead of only changes.
void pw64_frame_presented(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    (void)ctx;

    using clock = std::chrono::steady_clock;
    static clock::time_point window_start = clock::now();
    static uint32_t frames = 0;
    static int last_rate = -1;
    static uint32_t last_presented = 0;

    ++frames;
    const auto elapsed = clock::now() - window_start;
    if (elapsed < std::chrono::seconds(2)) {
        return;
    }

    const double seconds = std::chrono::duration<double>(elapsed).count();
    const int rate = static_cast<int>(std::lround(frames / seconds));
    window_start = clock::now();
    frames = 0;

    static const bool every_window = [] {
        const char* value = std::getenv("PW64_FRAME_STATS");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();

    const uint32_t presented = ultramodern::get_target_framerate(static_cast<uint32_t>(rate));
    if (rate == last_rate && presented == last_presented && !every_window) {
        return;
    }
    last_rate = rate;
    last_presented = presented;

    std::fprintf(stderr, "[pw64] the game is running at %d frames per second; presenting at %u"
                         " (display %u Hz)\n",
                 rate, presented, ultramodern::get_display_refresh_rate());
    std::fflush(stderr);
}

// See pw64_widescreen_factor in patches/patches.h. Returned in f0, where the
// MIPS calling convention puts a float result.
void pw64_widescreen_factor(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    float factor = 1.0f;
    if (ultramodern::renderer::get_graphics_config().ar_option ==
        ultramodern::renderer::AspectRatio::Expand) {
        factor = std::max(1.0f, pw64::window_aspect() / (4.0f / 3.0f));
    }
    ctx->f0.fl = factor;
}

// See pw64_hud_margin in patches/patches.h. This mirrors how RT64 turns the
// HUD Placement setting into its extended-origin percentage
// (rt64_workload_queue.cpp): Full is all of the widening, Clamp16x9 the part of
// it up to 16:9, Original none.
void pw64_hud_margin(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    const auto& config = ultramodern::renderer::get_graphics_config();
    constexpr float kFourThirds = 4.0f / 3.0f;

    float display = kFourThirds;
    if (config.ar_option == ultramodern::renderer::AspectRatio::Expand) {
        display = std::max(kFourThirds, pw64::window_aspect());
    }

    float percentage = 0.0f;
    switch (config.hr_option) {
        case ultramodern::renderer::HUDRatioMode::Full:
            percentage = 1.0f;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            if (display > kFourThirds) {
                percentage = std::clamp((16.0f / 9.0f - kFourThirds) / (display - kFourThirds), 0.0f, 1.0f);
            }
            break;
        default:
            break;
    }

    const float widening = display / kFourThirds;
    ctx->f0.fl = 160.0f * (widening - 1.0f) * percentage;
}

}  // extern "C"
