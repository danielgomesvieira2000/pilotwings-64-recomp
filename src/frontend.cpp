// Wires RecompFrontend's launcher, ROM picker and config menu in.
//
// See include/pw64/frontend.h for what this is and why it is optional.
//
// The division of labour is worth stating, because almost none of it is ours.
// RecompFrontend owns the launcher, the native file dialog, the ROM validation
// error messages, the settings tabs, the controller remapping and the profiles
// that persist between sessions. What a port supplies is three things: which
// game this is, what the menu entries should say, and a stylesheet.

#include "pw64/frontend.h"
#include "pw64/callbacks.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <recompui/recompui.h>
#include <recompui/config.h>
#include <recompui/program_config.h>
#include <recompui/renderer.h>
#include <recompinput/players.h>
#include <recompinput/input_mapping.h>
#include <recompinput/profiles.h>

#include <librecomp/config.hpp>
#include <librecomp/game.hpp>
#include <ultramodern/config.hpp>

#include "pw64/rom.h"

// The two globals recompui expects the port to define. It declares them extern
// in its own translation units and links against whatever the port provides:
//
//     extern std::vector<recomp::GameEntry> supported_games;   // ui_launcher.cpp
//     extern SDL_Window* window;                               // ui_state.cpp
//
// They are in the global namespace because that is where the library looks for
// them. `supported_games` is what the launcher falls back to when a port does
// not register its own init callback; it is populated anyway so the two
// descriptions of this game cannot drift apart.
std::vector<recomp::GameEntry> supported_games;
SDL_Window* window = nullptr;

namespace {

// Where recompui keeps its settings: the directory main.cpp registered with
// librecomp, which is what recompui's own load and save use. This used to look
// beside the executable, while librecomp -- never told otherwise -- wrote the
// files into whatever directory the game was started from. The two agreed only
// when the game was launched from its own directory, and disagreed silently
// otherwise: settings saved in the menu came back as defaults on the next run.
std::filesystem::path config_directory() {
    return recomp::get_config_path();
}

// The key bindings, in the file recompui itself loads them from in finalize().
std::filesystem::path controls_config_path() {
    return config_directory() / (recompui::config::controls::id + ".json");
}

ultramodern::renderer::PresentationMode presentation_mode();

// RecompFrontend's renderer draws the game and the menus into the same command
// list, so it replaces a renderer of the port's own rather than sitting beside
// it. The one thing the port decides is the presentation mode; see below.
std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(
        uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle,
        bool developer_mode) {
    return recompui::renderer::create_render_context(
        rdram, window_handle, presentation_mode(), developer_mode);
}

// Which frame RT64 puts on screen, and when.
//
// Console shows what the N64's video interface would have shown: the buffer
// the game finished a frame or more ago. That is the faithful choice, and it
// also switches the Framerate setting off. RT64 only generates frames between
// two game frames when the buffer it just drew is the one being presented,
// which under Console never happens for a game that buffers at all -- Wave Race
// 64's port found its Display and Manual options changed nothing until this was
// changed.
//
// PresentEarly shows each frame as soon as it is drawn, which is what the other
// recompiled ports do. It takes two frames of latency off, and it is what lets
// RT64 interpolate. SkipBuffering is the middle ground -- it presents the
// buffer the game meant to show, but as soon as the game has finished it --
// and also allows interpolation. PW64_PRESENT_MODE picks one by name for
// comparing them; it is a testing knob, not a setting.
ultramodern::renderer::PresentationMode presentation_mode() {
    using Mode = ultramodern::renderer::PresentationMode;
    const char* env = std::getenv("PW64_PRESENT_MODE");
    if (env != nullptr) {
        const std::string_view value{ env };
        if (value == "console") return Mode::Console;
        if (value == "skip")    return Mode::SkipBuffering;
        if (value == "early")   return Mode::PresentEarly;
        std::fprintf(stderr, "[pw64] PW64_PRESENT_MODE=%s not recognised (console, skip, early); using early\n", env);
    }
    return Mode::PresentEarly;
}

// Builds the launcher's menu. Called once, when the launcher is created.
//
// add_start_game_or_load_rom_option is the whole first-run flow: with no valid
// dump it reads "Load ROM" and opens a file dialog, validates what comes back
// against the hash this project is pinned to, and reports which way it failed;
// once a dump is accepted it becomes "Start Game" and calls into librecomp.
// Nothing about that is written here, which is the point of the library.
void build_launcher(recompui::LauncherMenu* menu) {
    recompui::GameOptionsMenu* options = menu->init_game_options_menu(
        std::u8string{ pw64::kGameId },
        pw64::kModGameId,
        pw64::kDisplayName,
        // No thumbnail: any artwork for the game itself would be taken from
        // the cartridge, and this project does not ship anything derived from
        // a dump. The launcher lays out fine without one.
        {});

    // The launcher's own background, distinct from the game thumbnail above:
    // this is original artwork supplied with the project (assets/icons/Logo.svg),
    // not derived from the cartridge, so it carries none of that restriction.
    // remove_default_title() takes down the plain-text program name the
    // library shows in its place -- the two would otherwise overlap.
    menu->set_launcher_background_svg("icons/Logo.svg");
    menu->remove_default_title();

    options->add_start_game_or_load_rom_option("Load ROM", "Start Game");
    options->add_setup_controls_option("Controls");
    options->add_settings_option("Settings");
    options->add_mods_option("Mods");
    // Closing the window works, but a menu the pad can reach should not need a
    // mouse to leave. add_exit_option calls ultramodern::quit(), which unwinds
    // the game thread and the renderer in order rather than tearing the process
    // down.
    options->add_exit_option("Quit");
}

}  // namespace

namespace pw64::frontend {

void init() {
    // The program's own identity, as distinct from the game's. recompui shows
    // the name in the launcher and uses the id to decide where settings and
    // controller profiles are stored, so both must be set before anything is
    // built -- the launcher throws from its constructor otherwise.
    recompui::programconfig::set_program_name("Pilotwings 64: Recompiled");
    recompui::programconfig::set_program_id(u8"Pilotwings64Recomp");

    // The family name is the one inside the font, not the filename: this file
    // is LatoLatin-Regular.ttf and declares itself "LatoLatin". Getting it wrong
    // is silent -- the UI lays out and draws with every element in place and no
    // text in any of them. The stylesheet has to name the same family.
    //
    // Lato is the face RmlUi vendors for its own samples, under the SIL Open
    // Font License; the build copies it next to the executable rather than
    // committing a second copy of a binary this repository already has.
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");

    // The keyboard layout this port has always documented, declared as the
    // frontend's defaults so that the keys in docs/BUILDING.md are the keys a
    // fresh profile is bound to. RecompFrontend's own defaults are a different
    // scheme -- WASD and space -- and once input started going through its
    // profiles, that scheme silently replaced this one.
    //
    // Defaults apply to a profile the first time it is created; a keyboard
    // profile already saved keeps whatever it holds until it is reset in the
    // controls tab.
    {
        using recompinput::GameInput;
        using recompinput::InputField;
        const struct { GameInput input; SDL_Scancode key; } keys[] = {
            { GameInput::X_AXIS_NEG,  SDL_SCANCODE_LEFT },
            { GameInput::X_AXIS_POS,  SDL_SCANCODE_RIGHT },
            { GameInput::Y_AXIS_POS,  SDL_SCANCODE_UP },
            { GameInput::Y_AXIS_NEG,  SDL_SCANCODE_DOWN },
            { GameInput::A,           SDL_SCANCODE_X },
            { GameInput::B,           SDL_SCANCODE_C },
            { GameInput::Z,           SDL_SCANCODE_Z },
            { GameInput::START,       SDL_SCANCODE_RETURN },
            { GameInput::L,           SDL_SCANCODE_A },
            { GameInput::R,           SDL_SCANCODE_S },
            // The C buttons work the camera views, so they stay under the right
            // hand while the left flies. The same layout as the other ports.
            { GameInput::C_UP,        SDL_SCANCODE_I },
            { GameInput::C_DOWN,      SDL_SCANCODE_K },
            { GameInput::C_LEFT,      SDL_SCANCODE_J },
            { GameInput::C_RIGHT,     SDL_SCANCODE_L },
            { GameInput::DPAD_UP,     SDL_SCANCODE_T },
            { GameInput::DPAD_DOWN,   SDL_SCANCODE_G },
            { GameInput::DPAD_LEFT,   SDL_SCANCODE_F },
            { GameInput::DPAD_RIGHT,  SDL_SCANCODE_H },
        };
        for (const auto& binding : keys) {
            recompinput::set_default_mapping_for_keyboard(
                binding.input, { InputField::keyboard(binding.key) });
        }
    }

    recompui::register_launcher_init_callback(build_launcher);

    // Pilotwings 64 is a one-player game, so the frontend runs in single-player
    // mode, as Rayman 2: Recompiled does. The Controls tab then shows one set of
    // keyboard and controller bindings to edit directly, instead of player slots
    // to assign devices to, and every connected pad and the keyboard play at
    // once -- nothing has to be assigned before the game responds.
    recompinput::players::set_player_count_range(1, 1);
    recompinput::players::set_single_player_mode(true);

    // The single-player keyboard and controller profiles, created now so the
    // defaults above are applied to them. finalize() would create them while
    // loading controls.json, but a first run has no such file.
    recompinput::profiles::initialize_input_bindings();

    // The prefab tabs. Pilotwings 64 has no gyro or mouse control and predates
    // the Rumble Pak, so the general tab keeps only what applies.
    recompui::config::GeneralTabOptions general{};
    general.has_rumble_strength = false;
    general.has_gyro_sensitivity = false;
    general.has_mouse_sensitivity = false;

    recompui::config::create_general_tab(general);

    // The Graphics tab is RecompFrontend's: resolution, aspect ratio, HUD
    // placement, frame rate, anti-aliasing and window mode are RT64 options the
    // library already exposes. The port's own graphics options join it as the
    // enhancements that need them are built.
    recompui::config::create_graphics_tab();

    // Main Volume did nothing in Wave Race 64's port until it was wired here:
    // recompui defines the slider and reads it back, and nothing upstream ever
    // applies it. The Sound tab has no Apply button, so the callback hears Load
    // when the saved setting is read at startup and Permanent on every step of
    // the slider, which is what makes the volume follow the handle as it moves.
    auto& sound = recompui::config::create_sound_tab();
    sound.add_option_change_callback(
        recompui::config::sound::options::main_volume,
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const double* percent = std::get_if<double>(&value)) {
                pw64::set_audio_volume(*percent);
            }
        });

    sound.add_bool_option(
        "mute_unfocused", "Mute When Not In Focus",
        "Silences the game while another window has focus.",
        true);
    sound.add_option_change_callback(
        "mute_unfocused",
        [](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant,
           recomp::config::OptionChangeContext) {
            if (const bool* mute = std::get_if<bool>(&value)) {
                pw64::set_mute_when_unfocused(*mute);
            }
        });
    recompui::config::create_controls_tab();

    // Mods. The runtime half of this has been running since the port first
    // started: recomp::start calls initialize_mods() and scan_mods() on its own,
    // main.cpp gives librecomp this game's mod id, and the mods and mod_config
    // folders have existed in the settings directory all along. What was missing
    // was any way to see what is in them -- so a mod could be installed and
    // never appear, never be enabled and never be reported broken.
    //
    // The tab lists what was found, with each mod's description, author, version
    // and its own options; the launcher entry below opens it without starting the
    // game first.
    recompui::config::create_mods_tab();

    // No add_game_input calls: recompinput already knows the N64 controller,
    // and this game has no inputs beyond it. Ports with extra actions -- an
    // ocarina, a quick-save -- declare them here so they appear in the
    // remapping list.

    // Whether this is a first run, asked before finalize(): loading a missing
    // settings file writes one with the defaults, so afterwards it always exists.
    const bool first_run = !std::filesystem::exists(config_directory() / "graphics.json");

    // Loads the player's saved settings from disk. Must come after every tab.
    recompui::config::finalize();

    // The frontend saves bindings only from the Controls tab's close handler, so
    // a rebind followed by closing the whole menu with Escape was lost, and a
    // fresh install had no controls.json at all. The defaults are written now if
    // there is none, and shutdown() writes whatever is in effect on the way out.
    if (!std::filesystem::exists(controls_config_path())) {
        recompinput::profiles::save_controls_config(controls_config_path());
    }

    // Play fullscreen at the display's own resolution and aspect ratio.
    //
    // Two thirds of that are already the library's defaults: resolution is Auto,
    // which renders at the window's true pixel size rather than upscaling
    // 320x240, and aspect ratio is Expand, which widens the frustum to whatever
    // shape the window is. Only the window mode defaults to Windowed.
    //
    // Only when the player had no saved graphics settings, which is what makes it
    // a first-run default rather than an override: choose Windowed in the menu and
    // that choice is saved and respected from then on. It is written after
    // finalize() -- the option map does not exist until the file has loaded, and
    // writing into it faults.
    //
    // The Graphics tab confirms its changes, so set_option_value only stages the
    // value; save_config() applies it, which is what the Apply button does, and
    // writes it. Without that the menu showed Fullscreen as an unapplied change
    // and nothing was saved. ultramodern's copy, which the renderer reads, is set
    // as well, so the first window opens fullscreen either way.
    //
    if (first_run) {
        auto& graphics_config = recompui::config::get_graphics_config();
        graphics_config.set_option_value(
            recompui::config::graphics::options::wm_option,
            static_cast<uint32_t>(ultramodern::renderer::WindowMode::Fullscreen));
        graphics_config.save_config();

        ultramodern::renderer::GraphicsConfig gfx = ultramodern::renderer::get_graphics_config();
        gfx.wm_option = ultramodern::renderer::WindowMode::Fullscreen;
        ultramodern::renderer::set_graphics_config(gfx);

        std::fprintf(stderr, "[pw64] no saved graphics settings; defaulting to fullscreen at the display\'s size\n");
    }

    // Test hook: PW64_TEST_OPEN_SETTINGS=graphics@25 opens the settings menu on the
    // Graphics tab 25 seconds after startup, so a capture can show a tab as a
    // player sees it without anyone pressing Escape. Opening it changes nothing
    // that is saved.
    if (const char* spec = std::getenv("PW64_TEST_OPEN_SETTINGS")) {
        const std::string text = spec;
        const size_t at = text.find('@');
        const std::string tab = text.substr(0, at);
        const double delay = at == std::string::npos ? 20.0 : std::atof(text.c_str() + at + 1);
        std::thread([tab, delay]() {
            std::this_thread::sleep_for(std::chrono::duration<double>(delay));
            recompui::ContextId context = recompui::config::get_config_context_id();
            context.open();
            recompui::config::set_tab(tab);
            context.close();
            recompui::config::open();
            std::fprintf(stderr, "[pw64] test: opened the settings menu on the %s tab\n", tab.c_str());
            std::fflush(stderr);
        }).detach();
    }

    std::fprintf(stderr, "[pw64] frontend ready: launcher, ROM picker and config menu\n");
    std::fflush(stderr);
}

void publish_game(const recomp::GameEntry& game) {
    supported_games.clear();
    supported_games.push_back(game);
}

void publish_window(SDL_Window* sdl_window) {
    ::window = sdl_window;
}

ultramodern::renderer::callbacks_t renderer_callbacks() {
    ultramodern::renderer::callbacks_t callbacks{};
    callbacks.create_render_context = create_render_context;
    return callbacks;
}

bool capturing_input() {
    return recompui::is_context_capturing_input();
}

void shutdown() {
    recompinput::profiles::save_controls_config(controls_config_path());
}

}  // namespace pw64::frontend
