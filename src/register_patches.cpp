// Hands the recompiled C patches to librecomp.
//
// tools/build_patches.py writes three things into RecompiledPatches/: the
// recompiled functions (patches.c, compiled into the executable), their section
// table (recomp_overlays.inl, included here, since its symbols are static), and
// the patches' own data (patches.bin, embedded as patches_bin.c), which librecomp
// copies to 0x80801000 before the game starts.
//
// A patch replaces the game's function by being the only definition of its
// name: the original is `ignored` in recomp/pilotwings64.us.toml, so direct calls
// in the game reach the patch. Calls through a pointer look the original address
// up, so each patch is registered at it (register_patched_addresses, from the
// on_init hook, since librecomp clears the function map before that). The
// manual symbols are what let mods resolve the host functions patches call.

#include <cstddef>

#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/sections.h"

#include "../RecompiledPatches/recomp_overlays.inl"

#include "../RecompiledPatches/patched_addresses.inl"

extern "C" const size_t pw64_patches_bin_size;
extern "C" const unsigned char pw64_patches_bin[];

namespace pw64 {

void register_patches() {
    recomp::overlays::register_patches(reinterpret_cast<const char*>(pw64_patches_bin),
                                       pw64_patches_bin_size, section_table, ARRLEN(section_table));
    recomp::overlays::register_base_exports(export_table);
    recomp::overlays::register_manual_patch_symbols(manual_patch_symbols);
}

void register_patched_addresses() {
    for (size_t i = 0; pw64_patched_addresses[i].func != nullptr; ++i) {
        recomp::overlays::add_loaded_function(pw64_patched_addresses[i].vram, pw64_patched_addresses[i].func);
    }
}

}  // namespace pw64
