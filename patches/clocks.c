// Busy waits: letting the rest of the game run while a thread spins on the clock.
//
// The game waits in several places by spinning on a clock without calling the
// OS at all:
//
//   uvaSeqStop (kernel/audio_seq.c)  until the audio thread has stopped the
//                                    music, or two seconds have passed
//   func_803434E8 (app/title_screen.c)  one second
//   the photo album (app/snap.c)     a tenth of a second per photograph
//
// On the N64 those loops are preempted: the audio thread is woken by its
// retrace message and runs, stops the sequence, and the loop ends within a
// frame. The runtime switches game threads only inside OS calls, so in the port
// nothing else runs while the loop spins. Every music change ran uvaSeqStop into
// its two-second timeout, up to three times: entering the file menu took six
// seconds, on a stale frame, with the audio starved throughout.
//
// All of these loops read uvClkGetSec, so that is where the port lets waiting
// threads in: before reading the clock it delivers any pending runtime event
// (a retrace, an audio interrupt) and switches to a higher-priority thread made
// ready by it. That is what preemption at that instant would have done, and on
// a thread with nothing to switch to it costs a queue check.

#include "patches.h"

#include "uv_clocks.h"

extern uvClockState_t gClockState[UV_CLKID_COUNT];
extern s32 gClockWraps;
extern u32 gClockTicks;

// The decompilation's uvClkGetSec (src/kernel/clocks.c), after pw64_poll_threads.
RECOMP_PATCH f64 uvClkGetSec(s32 clkId) {
    f64 secsFromWraps;
    f64 secsFromTicks;

    pw64_poll_threads();

    if (clkId >= UV_CLKID_COUNT) {
        _uvDebugPrintf("uvClkGetSec: unknown clock %d\n", clkId);
        return 0.0;
    }

    uvClkUpdate();
    secsFromWraps = gClockWraps - gClockState[clkId].wraps;
    secsFromWraps *= UV_CLK_WRAP_SECS;
    secsFromTicks = ((f64)gClockTicks - gClockState[clkId].ticks) / UV_CLK_TICK_FREQ;

    return secsFromWraps + secsFromTicks;
}
