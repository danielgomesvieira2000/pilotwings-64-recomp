// libultra functions the recompiler skipped that the runtime does not supply
// either.
//
// N64Recomp keeps an `ignored_funcs` list of libultra routines it will not
// translate, on the basis that the runtime provides them. librecomp implements
// most, but not all: the ones below are named in that list, are called by the
// game, and exist nowhere else. Without them the link fails on undefined symbols with
// no hint as to whose job they were.
//
// Five are Controller Pak routines. Pilotwings 64 saves to EEPROM, and the
// Controller Pak is only consulted to discover that none is attached, so
// reporting "no pak" is the behaviour the game would see on hardware with an
// empty controller slot -- not a placeholder. librecomp's own pak.cpp answers
// its share of the Controller Pak API exactly this way, and these follow it.
//
// The sixth reads a COP0 register that has no meaning off the console.

#include <span>

#include "ultramodern/ultra64.h"
#include "recomp.h"
#include "librecomp/addresses.hpp"
#include "librecomp/game.hpp"

extern "C" {

// PFS_ERR_NOPACK: no memory card plugged in. Return values arrive in v0.
constexpr uint32_t kPfsErrNoPack = 1;

void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    // Reports zero paks plugged in via the bitmask argument as well as the
    // error code, so callers that check either path agree.
    ctx->r2 = kPfsErrNoPack;
}

void osPfsInit_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPack;
}

void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPack;
}

void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPack;
}

void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = kPfsErrNoPack;
}

// The COP0 Cause register describes why an exception was taken. Nothing raises
// one here: ultramodern handles threading and exceptions on the host, so there
// is no pending cause to report and zero is the truthful answer.
void __osGetCause_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

// libultra's kernel debug server, from libultra/os/kdebugserver.s. It writes
// debug packets to the host over the development board's link, hardware no
// player has and this port cannot emulate. The game only reaches it through
// debug paths that a retail build never takes, so doing nothing is correct
// rather than merely convenient.
void send_packet_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = 0;
}

// osPiReadIo and osPiRawReadIo read one word from the cartridge through the PI's
// I/O window rather than by DMA. Pilotwings 64 calls both, and librecomp
// implements neither, so without these the port does not link:
//
// - bootproc reads 64 bytes at cartridge offset 0xFFB000 before anything else
//   runs, looking for a "-d" or "-z" marker a development cartridge carried
//   there. A retail 8 MB ROM ends at 0x800000, so there is nothing at that
//   offset and the answer is zero -- which is the "retail cartridge" branch.
// - _uvMediaCopy finishes a copy to a misaligned destination with two PIO reads
//   of the ROM, where a DMA would need an aligned target.
//
// Both behave as libultra's do: the address is relative to osRomBase, the word
// is read big-endian, and the return value is 0. A read past the end of the dump
// yields zero, as the open bus effectively does for these callers.
namespace {

void pi_read_word(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dev_addr = static_cast<uint32_t>(ctx->r4);
    const gpr data = ctx->r5;
    const uint32_t physical = (dev_addr | recomp::rom_base) & 0x1FFFFFFFu;

    const std::span<const uint8_t> rom = recomp::get_rom();
    const uint64_t offset = static_cast<uint64_t>(physical) - recomp::rom_base;
    for (int i = 0; i < 4; ++i) {
        const uint64_t at = offset + static_cast<uint64_t>(i);
        MEM_B(i, data) = at < rom.size() ? static_cast<int8_t>(rom[at]) : 0;
    }
    ctx->r2 = 0;
}

}  // namespace

void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    pi_read_word(rdram, ctx);
}

void osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    pi_read_word(rdram, ctx);
}

}  // extern "C"
