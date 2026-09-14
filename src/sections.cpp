// Hands the generated section table to librecomp.
//
// N64Recomp emits recomp_overlays.inl with a section_table describing every
// code section -- its ROM and RAM addresses, size, functions and relocations --
// and the list of which sections are relocatable overlays. The symbols are
// `static` in the generated file, so this is the one translation unit that can
// see them, which is why the .inl is included rather than linked against.
//
// Pilotwings 64 has no overlays: its two code segments, kernel and app, are
// loaded once at boot and never move. So the overlay list is empty, every call
// in the generated code is direct, and nothing like Wave Race 64's runtime
// dispatch is needed.
//
// What does need care is the function map that calls through a pointer are
// resolved against -- the game's state machine, for one, is a table of them.
// librecomp fills it at boot the way IPL3 loads a cartridge: it assumes the
// first megabyte of ROM from 0x1000 lands contiguously at the entry point, and
// registers every section in that range at that position. That is right for
// the kernel and wrong for the app segment. The kernel is followed in RAM by
// 0x79A80 bytes of bss that the ROM does not contain, so the app's code sits
// at 0x802CA900, not at the 0x80250E80 a contiguous load implies -- the boot
// code copies it there itself, with _uvMediaCopy(app_TEXT_START, app_ROM_START,
// ...) in the decompilation's src/kernel/system.c. Left alone, every app
// function is registered 0x79A80 bytes too low and the first indirect call into
// the app fails ("no function registered at 0x802EBC94", from gameUpdate).

#include <cstddef>
#include <cstdio>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"

#include "recomp_overlays.inl"

#include "pw64/callbacks.h"

namespace pw64 {

void register_sections() {
    recomp::overlays::overlay_section_table_data_t sections{};
    sections.code_sections = section_table;
    sections.num_code_sections = ARRLEN(section_table);
    sections.total_num_sections = num_sections;

    recomp::overlays::overlays_by_index_t overlays{};
    overlays.table = overlay_sections_by_index;
    overlays.len = ARRLEN(overlay_sections_by_index);

    recomp::overlays::register_overlays(sections, overlays);
}

void place_resident_sections(uint32_t entrypoint) {
    // The same arithmetic librecomp's boot load uses, to find where it put each
    // section; any section whose linked address differs is moved to it.
    constexpr uint32_t kBootRom = 0x1000;
    constexpr uint32_t kBootSize = 1024 * 1024;
    for (size_t i = 0; i < ARRLEN(section_table); ++i) {
        const SectionTableEntry& section = section_table[i];
        if (section.rom_addr < kBootRom || section.rom_addr + section.size > kBootRom + kBootSize) {
            continue;
        }
        const uint32_t booted_at = section.rom_addr - kBootRom + entrypoint;
        if (booted_at == section.ram_addr) {
            continue;
        }
        unload_overlays(static_cast<int32_t>(booted_at), section.size);
        load_overlays(section.rom_addr, static_cast<int32_t>(section.ram_addr), section.size);
        std::fprintf(stderr, "[pw64] section at ROM 0x%06X registered at 0x%08X (librecomp's boot"
                             " load put it at 0x%08X)\n",
                     section.rom_addr, section.ram_addr, booted_at);
    }
    std::fflush(stderr);
}

size_t code_section_count() {
    return ARRLEN(section_table);
}

}  // namespace pw64
