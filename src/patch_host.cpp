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
#include <thread>

#include "recomp.h"

#include <ultramodern/ultramodern.hpp>
#include <ultramodern/config.hpp>

#include "pw64/callbacks.h"

extern "C" {

// Added to RT64 by tools/patch_rt64_pairing.py.
void RT64_GetTransformPairing(unsigned long long* frames, unsigned long long* total, unsigned long long* ignored,
                              unsigned long long* unpaired, unsigned long long* unpaired_moved);

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

    // Test knob: PW64_GAME_RATE=30 holds the game to 30 frames per second (or any
    // rate below the display's). In the port the game runs at the VI rate, 60,
    // which on a 60 Hz display leaves RT64 no frames to generate -- so on such a
    // machine interpolation cannot be seen, let alone checked, without slowing
    // the game down. The game measures its own frame time, so it plays at normal
    // speed, just in fewer steps. Sleeping here stalls the scheduler thread, which
    // is the point; the audio may crackle while it is set. Not a setting.
    static const double game_rate = [] {
        const char* value = std::getenv("PW64_GAME_RATE");
        return value != nullptr ? std::atof(value) : 0.0;
    }();
    if (game_rate > 0.0) {
        static clock::time_point next_frame = clock::now();
        const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / game_rate));
        const auto now = clock::now();
        if (now < next_frame) {
            std::this_thread::sleep_until(next_frame);
            next_frame += period;
        } else {
            next_frame = now + period;
        }
    }
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

    // PW64_PAIRING=1: how well RT64 is pairing transforms for interpolation, over
    // the same window (tools/patch_rt64_pairing.py). Only meaningful while RT64 is
    // generating frames, which needs the display faster than the game.
    static const bool pairing = std::getenv("PW64_PAIRING") != nullptr;
    if (pairing) {
        unsigned long long p_frames = 0, p_total = 0, p_ignored = 0, p_unpaired = 0, p_moved = 0;
        RT64_GetTransformPairing(&p_frames, &p_total, &p_ignored, &p_unpaired, &p_moved);
        static unsigned long long l_frames = 0, l_total = 0, l_ignored = 0, l_unpaired = 0, l_moved = 0;
        const unsigned long long d_frames = p_frames - l_frames;
        if (d_frames > 0) {
            std::fprintf(stderr,
                         "[pw64] interpolation: %.0f transforms a frame, %.1f not interpolated by request,"
                         " %.1f unpaired (%.1f of them moved or new)\n",
                         double(p_total - l_total) / d_frames, double(p_ignored - l_ignored) / d_frames,
                         double(p_unpaired - l_unpaired) / d_frames, double(p_moved - l_moved) / d_frames);
            std::fflush(stderr);
        }
        l_frames = p_frames;
        l_total = p_total;
        l_ignored = p_ignored;
        l_unpaired = p_unpaired;
        l_moved = p_moved;
    }

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

// A diagnostic print from a patch: a tag and three integers (a0-a3), printed
// when PW64_PATCH_DEBUG is set.
void pw64_debug(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    static const bool enabled = std::getenv("PW64_PATCH_DEBUG") != nullptr;
    if (!enabled) {
        return;
    }
    std::fprintf(stderr, "[pw64-patch] tag %u: %d %d %d\n", static_cast<uint32_t>(ctx->r4),
                 static_cast<int32_t>(ctx->r5), static_cast<int32_t>(ctx->r6),
                 static_cast<int32_t>(ctx->r7));
}

// See pw64_poll_threads in patches/patches.h. A zero timeout makes the wait a
// poll: at most one event is taken off the runtime's queue.
void pw64_poll_threads(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
    ultramodern::wait_for_external_message_timed(rdram, 0);
    ultramodern::check_running_queue(rdram);
}

// See pw64_profile_mark in patches/patches.h.
void pw64_profile_mark(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    using clock = std::chrono::steady_clock;
    static const bool enabled = std::getenv("PW64_PATCH_DEBUG") != nullptr;
    if (!enabled) {
        return;
    }

    constexpr int kTags = 16;
    static clock::time_point last = clock::now();
    static clock::time_point report = clock::now();
    static double seconds[kTags] = {};
    static uint32_t counts[kTags] = {};

    const auto now = clock::now();
    const uint32_t tag = static_cast<uint32_t>(ctx->r4) % kTags;
    seconds[tag] += std::chrono::duration<double>(now - last).count();
    counts[tag]++;
    last = now;

    // Tag 0 reports at once, for a mark at the end of what is being measured.
    if (tag != 0 && now - report < std::chrono::seconds(2)) {
        return;
    }
    report = now;
    std::fprintf(stderr, "[pw64-profile]");
    for (int i = 0; i < kTags; i++) {
        if (counts[i] != 0) {
            std::fprintf(stderr, " %d: %u x %.2f ms;", i, counts[i], seconds[i] * 1000.0 / counts[i]);
        }
        seconds[i] = 0.0;
        counts[i] = 0;
    }
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

// See pw64_interp_tags_enabled in patches/patches.h.
void pw64_interp_tags_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    static const bool disabled = std::getenv("PW64_NO_INTERP_TAGS") != nullptr;
    ctx->r2 = disabled ? 0 : 1;
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
