#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pw64 {

// Byte order of an N64 dump, identified from the first word of the header.
enum class RomFormat {
    Unknown,
    Z64,  // big endian,     0x80371240 -- the only format the toolchain accepts
    N64,  // little endian,  0x40123780
    V64,  // byte swapped,   0x37804012
};

// The fields of the 64-byte N64 cartridge header that identify a dump.
struct RomHeader {
    RomFormat   format = RomFormat::Unknown;
    uint32_t    crc1 = 0;          // header offset 0x10
    uint32_t    crc2 = 0;          // header offset 0x14
    std::string internal_name;     // header offset 0x20, 20 bytes, space padded
    std::string cartridge_id;      // header offset 0x3C, 2 bytes  ("PW" for Pilotwings)
    char        region = '\0';     // header offset 0x3E          ('E' = USA)
    uint8_t     revision = 0;      // header offset 0x3F
    uint64_t    size_bytes = 0;
};

// What the recompilation pipeline is pinned to: Pilotwings 64 (USA), the one
// dump the decompilation matches.
//
//   sha1  ec771aedf54ee1b214c25404fb4ec51cfd43191a
//
// That sha1 is the value lib/Pilotwings64Decomp pins in
// config/us/pilotwings64.us.sha1, so a dump matching it is the one every symbol
// and every function boundary in the generated code was built from.
inline constexpr char        kTargetCartridgeId[] = "PW";
inline constexpr char        kTargetRegion  = 'E';
inline constexpr uint8_t     kTargetRevision = 0;
inline constexpr uint64_t    kTargetSizeBytes = 8u * 1024u * 1024u;

// CRC1/CRC2 from the header of the pinned dump. Setting both to zero disables
// the check and leaves only the structural ones.
inline constexpr uint32_t    kTargetCrc1 = 0xC851961C;
inline constexpr uint32_t    kTargetCrc2 = 0x78FCAAFA;

std::string to_string(RomFormat format);

// Reads the first 64 bytes and the file size. Does not load the ROM.
bool read_header(const std::filesystem::path& path, RomHeader& out, std::string& error);

// Structural verification against the pinned target above. Populates `problems`
// with one human-readable line per mismatch; returns true when there are none.
bool verify(const RomHeader& header, std::vector<std::string>& problems);

// How librecomp and RecompFrontend identify this game and its dump.
//
// kRomHash is XXH3-64 of the whole z64 image, which is what librecomp checks a
// dump against. The launcher's ROM picker validates whatever file the player
// chooses against it, so it lives here for both callers.
inline constexpr uint64_t kRomHash = 0x887AB02583C90111ULL;
inline constexpr char8_t  kGameId[] = u8"pilotwings64.us";
inline constexpr char     kModGameId[] = "pilotwings64";
inline constexpr char     kDisplayName[] = "Pilotwings 64";

// Where the cartridge's boot code jumps to, from the header and the ELF.
inline constexpr uint32_t kEntrypoint = 0x80200050u;

}  // namespace pw64
