// Scripted input and a game-state transcript, for verifying runs.
//
// See include/pw64/testdrive.h for why this exists. In short: from outside the
// process a port stuck on the title screen is indistinguishable from one that is
// flying, and a run driven by hand cannot be repeated.

#include "pw64/testdrive.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL.h>

namespace {

// ------------------------------------------------------------ input script ---

// N64 controller button bits, as libultra defines them. Duplicated from
// callbacks.cpp deliberately: this file is a test harness and should not make
// the input path depend on it.
struct NamedButton { const char* name; uint16_t bit; };
constexpr NamedButton kButtons[] = {
    { "A",      0x8000 }, { "B",      0x4000 }, { "Z",      0x2000 },
    { "START",  0x1000 }, { "DUP",    0x0800 }, { "DDOWN",  0x0400 },
    { "DLEFT",  0x0200 }, { "DRIGHT", 0x0100 }, { "L",      0x0020 },
    { "R",      0x0010 }, { "CUP",    0x0008 }, { "CDOWN",  0x0004 },
    { "CLEFT",  0x0002 }, { "CRIGHT", 0x0001 },
};

struct ScriptEntry {
    double start_seconds = 0.0;
    double end_seconds = 0.0;
    uint16_t buttons = 0;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    std::string note;
    bool announced = false;
    int player = 0;     // 0 for player one; a "2:" prefix on the buttons makes it 1
};

std::vector<ScriptEntry> g_script;
bool g_script_loaded = false;
bool g_script_has_player_two = false;
Uint64 g_script_start_ticks = 0;

double now_seconds() {
    if (g_script_start_ticks == 0) {
        g_script_start_ticks = SDL_GetTicks64();
    }
    return static_cast<double>(SDL_GetTicks64() - g_script_start_ticks) / 1000.0;
}

bool parse_buttons(const std::string& field, uint16_t* out) {
    *out = 0;
    if (field == "-" || field.empty()) {
        return true;
    }
    std::stringstream parts{ field };
    std::string name;
    while (std::getline(parts, name, ',')) {
        const NamedButton* found = nullptr;
        for (const NamedButton& candidate : kButtons) {
            if (name == candidate.name) {
                found = &candidate;
                break;
            }
        }
        if (found == nullptr) {
            std::fprintf(stderr, "[pw64] input script: unknown button '%s'\n", name.c_str());
            return false;
        }
        *out |= found->bit;
    }
    return true;
}

}  // namespace

namespace pw64 {

// Script format, one entry per line, '#' starts a comment:
//
//     <start> <end> <buttons> [stick_x stick_y] [# note]
//
// Times are seconds since the window opened; buttons are comma-separated names
// or '-' for none; the stick is in the N64's own +/-80 range. Entries overlap
// freely and are OR'd together, which is what makes both a tap (start 4.0, end
// 4.2) and a hold (accelerate for thirty seconds while steering) expressible in
// the same file without two syntaxes.
//
// Buttons prefixed with "2:" ("2:A", "2:-") are player two's. A script that
// uses the prefix at all makes the port report a second controller connected
// (see callbacks.cpp), which is what lets a script reach 2P VS without a second
// pad -- a script with "2:" lines stands in for a second pad.
bool load_input_script() {
    const char* path = std::getenv("PW64_INPUT_SCRIPT");
    if (path == nullptr) {
        return false;
    }

    std::ifstream file{ path };
    if (!file) {
        std::fprintf(stderr, "[pw64] input script: cannot open %s\n", path);
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const size_t comment = line.find('#');
        std::string note;
        if (comment != std::string::npos) {
            note = line.substr(comment + 1);
            line = line.substr(0, comment);
        }
        std::stringstream fields{ line };
        ScriptEntry entry;
        std::string button_field;
        if (!(fields >> entry.start_seconds >> entry.end_seconds >> button_field)) {
            continue;  // blank or comment-only line
        }
        // "2:A" is player two's A, "2:-" player two holding the stick alone.
        // A script that mentions player two makes that controller present, so
        // two-player modes can be reached without a second pad.
        if (button_field.rfind("2:", 0) == 0) {
            entry.player = 1;
            button_field = button_field.substr(2);
            g_script_has_player_two = true;
        }
        if (!parse_buttons(button_field, &entry.buttons)) {
            std::fprintf(stderr, "[pw64] input script: %s:%d\n", path, line_number);
            return false;
        }
        fields >> entry.stick_x >> entry.stick_y;  // optional

        // Trim the note so the log reads cleanly.
        const size_t first = note.find_first_not_of(" \t");
        if (first != std::string::npos) {
            entry.note = note.substr(first);
        }
        g_script.push_back(entry);
    }

    std::sort(g_script.begin(), g_script.end(),
              [](const ScriptEntry& a, const ScriptEntry& b) {
                  return a.start_seconds < b.start_seconds;
              });

    g_script_loaded = !g_script.empty();
    std::fprintf(stderr, "[pw64] input script: %zu entries from %s\n", g_script.size(), path);
    std::fflush(stderr);
    return g_script_loaded;
}

bool input_script_has_player_two() {
    return g_script_loaded && g_script_has_player_two;
}

void input_script_state(uint16_t* buttons, float* stick_x, float* stick_y) {
    input_script_state(0, buttons, stick_x, stick_y);
}

void input_script_state(int player, uint16_t* buttons, float* stick_x, float* stick_y) {
    *buttons = 0;
    *stick_x = 0.0f;
    *stick_y = 0.0f;
    if (!g_script_loaded) {
        return;
    }

    const double t = now_seconds();
    for (ScriptEntry& entry : g_script) {
        if (entry.player != player || t < entry.start_seconds || t >= entry.end_seconds) {
            continue;
        }
        if (!entry.announced) {
            entry.announced = true;
            std::fprintf(stderr, "[pw64] t=%6.2f input: %s\n", t,
                         entry.note.empty() ? "(no note)" : entry.note.c_str());
            std::fflush(stderr);
        }
        *buttons |= entry.buttons;
        // Last writer wins for the stick; entries are sorted by start time, so
        // a later overlapping entry is the more specific instruction.
        if (entry.stick_x != 0.0f) { *stick_x = entry.stick_x; }
        if (entry.stick_y != 0.0f) { *stick_y = entry.stick_y; }
    }
}

// ------------------------------------------------------- game state watch ---

namespace {

uint8_t* g_rdram = nullptr;

// GameState, from the decompilation's src/app/game.h. gameUpdate() indexes a
// table of update functions with it, so these are all the states there are.
const char* state_name(uint32_t state) {
    static const char* const kNames[] = {
        "TITLE", "STATE_1", "TEST_DETAILS", "PILOT_SELECT", "TEST_SETUP",
        "TEST_UPDATE", "RESULTS", "OPTIONS", "DEMO_PILOT", "DEMO_TEST_SETUP",
        "FILE_MENU", "VEHICLE_CLASS_SELECT", "TEST_OVERVIEW", "RESULTS_CB",
        "CONGRATULATIONS", "CREDITS",
    };
    return state < sizeof(kNames) / sizeof(kNames[0]) ? kNames[state] : nullptr;
}

// VehicleId, from src/app/task.h.
const char* vehicle_name(uint32_t vehicle) {
    static const char* const kNames[] = {
        "HANG_GLIDER", "ROCKET_BELT", "GYROCOPTER", "CANNONBALL",
        "SKY_DIVING", "JUMBLE_HOPPER", "BIRDMAN",
    };
    return vehicle < sizeof(kNames) / sizeof(kNames[0]) ? kNames[vehicle] : nullptr;
}

// The game's state block, D_80362698 in the decompilation (typed Unk80362690
// there): `s32 state` at +0, `u16 map` at +4, and the one player's record from
// +0xC, whose `u16 pilot` and `u16 veh` come first.
constexpr uint32_t kGameBlock   = 0x80362698;
constexpr uint32_t kGameState   = kGameBlock + 0x00;
constexpr uint32_t kGameMap     = kGameBlock + 0x04;
constexpr uint32_t kGamePilot   = kGameBlock + 0x0C;
constexpr uint32_t kGameVehicle = kGameBlock + 0x0E;

// RDRAM is stored with each 32-bit word in host order, so a word is a plain load
// at its physical offset, and a 16-bit field -- which is one half of a word --
// sits at its offset XOR 2. This is the same arithmetic the MEM_* macros in the
// recompiled code do.
uint32_t read_word(uint32_t vaddr) {
    return *reinterpret_cast<const uint32_t*>(g_rdram + (vaddr & 0x00FFFFFFu));
}

uint16_t read_half(uint32_t vaddr) {
    return *reinterpret_cast<const uint16_t*>(g_rdram + ((vaddr ^ 2u) & 0x00FFFFFFu));
}

}  // namespace

void set_rdram_base(uint8_t* rdram) {
    g_rdram = rdram;
}

uint32_t current_game_state() {
    return g_rdram != nullptr ? read_word(kGameState) : 0;
}

void poll_game_state() {
    if (g_rdram == nullptr) {
        return;
    }

    static uint32_t last_state = 0xFFFFFFFFu;
    static uint32_t last_vehicle = 0xFFFFFFFFu;
    static uint32_t last_map = 0xFFFFFFFFu;

    const uint32_t state = read_word(kGameState);
    const uint32_t vehicle = read_half(kGameVehicle);
    const uint32_t map = read_half(kGameMap);
    if (state == last_state && vehicle == last_vehicle && map == last_map) {
        return;
    }
    last_state = state;
    last_vehicle = vehicle;
    last_map = map;

    const char* state_text = state_name(state);
    const char* vehicle_text = vehicle_name(vehicle);
    char state_buf[16];
    char vehicle_buf[16];
    if (state_text == nullptr) {
        std::snprintf(state_buf, sizeof(state_buf), "%u", state);
        state_text = state_buf;
    }
    if (vehicle_text == nullptr) {
        std::snprintf(vehicle_buf, sizeof(vehicle_buf), "%u", vehicle);
        vehicle_text = vehicle_buf;
    }

    std::fprintf(stderr, "[pw64] t=%6.2f state: %s (vehicle %s, pilot %u, map %u)\n",
                 now_seconds(), state_text, vehicle_text, read_half(kGamePilot), map);
    std::fflush(stderr);
}

}  // namespace pw64
