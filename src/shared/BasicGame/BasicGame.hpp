/**
 * BasicGame - base class for game-support plugins.
 *
 * The MO2 modorganizer-basic_games "BasicGame" shape, adapted to the GMM
 * stable C ABI: the full registration is implemented ONCE here with
 * reasonable defaults for everything; a new game is a tiny subclass filling
 * a descriptor. Anything untouched rides the default:
 *
 *   struct MyGame : gmm::BasicGame {
 *     MyGame() {
 *       game_id      = "MyGame";   // == CMake target name
 *       display_name = "My Game";
 *       game_binary  = "mygame.exe";
 *       mods_subpath = "mods";
 *       steam_appid  = 12345;
 *     }
 *   };
 *   REGISTER_BASIC_GAME(MyGame)
 *
 * Rules carried here (a subclass must not re-derive them):
 * - Stable C ABI only: includes plugin.hpp (ABI repo), never Core headers.
 * - The .so target name MUST equal game_id: Core defaults an instance's
 *   game_id to the .so stem before register_game runs (Core
 *   plugin_loader.cpp), so a mismatched rename splits instance identity.
 * - Every hook tag registered here is verified live - Core reads it (the
 *   consumer is cited per tag). Dead tags (mod_counter_label,
 *   steam_app_name, game_versions, auto_sort_groups) are excluded on
 *   purpose; do not add a tag without grepping Core for a reader -
 *   tools/check_hook_tags.sh enforces this for this header.
 * - An unset hook is OMITTED, never registered with a placeholder value:
 *   an absent tag beats a dead one.
 * - No gmm_abi_features export: omitting it is the safe v2.0 baseline.
 *   Add it only in a subclass whose save parser fills v2.1 GmmSaveDataV2
 *   fields, mirroring Core's GMM_FEATURE_* bit numbering (see
 *   SkyrimSpecialEdition.cpp).
 * - Base-game filtering is a HARD CONSTRAINT: a game whose base install
 *   ships plugin/archive files MUST set game_native_plugins (see
 *   knowledgebase/cpp/agent-rules.md "Base Game Filtering").
 */

#ifndef GMM_BASIC_GAME_HPP
#define GMM_BASIC_GAME_HPP

#include "plugin.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace gmm {

class BasicGame {
public:
  virtual ~BasicGame() = default;

  /* -- REQUIRED - registration refuses to run (loud stderr error, nothing
   * registered) when any of these is empty. -- */

  /* game_id: MUST equal the CMake target name (the .so stem). */
  const char *game_id = nullptr;
  /* display_name: human-readable game name (UI, instance list). */
  const char *display_name = nullptr;
  /* game_binary: main executable, relative to the game dir. Also the
   * default for the exe_windows game() field and the executables hook. */
  const char *game_binary = nullptr;
  /* mods_subpath: mod root, relative to the game dir (Skyrim "Data",
   * Isaac "mods"). Also the default for the deploy_prefix hook. */
  const char *mods_subpath = nullptr;

  /* -- OPTIONAL - untouched fields ride the documented default. -- */

  /* Steam appid. 0 = not on Steam (hook omitted). Core matches it against
   * the user's Steam library.vdf for detection (core.cpp:714-727). */
  uint32_t steam_appid = 0;
  /* GOG id / Epic namespace. Null = omitted. */
  const char *gog_id         = nullptr;
  const char *epic_namespace = nullptr;
  /* Nexus domain slug. Default: game_id lowercased. When set (or
   * defaulted), the Downloads tab is Nexus-wired (nxm protocol). */
  const char *nexus_domain = nullptr;
  /* Per-OS exe names. Default: null (Linux uses Proton, macOS
   * unsupported - the Skyrim pattern). */
  const char *exe_linux = nullptr;
  const char *exe_macos = nullptr;
  /* Plugin metadata. author default: "GameModManager Team".
   * description default: "Adds basic support for game <display_name>."
   * version: always the VERSION build define - never a hardcoded number. */
  const char *author      = "GameModManager Team";
  const char *description = nullptr;
  /* Comma-separated exe names for the executables hook.
   * Default: game_binary. */
  const char *executables = nullptr;
  /* Per-user config dirs. Default for both: display_name (MO2's
   * documents default is Documents/My Games/<GameName>; our Skyrim plugin
   * sets both to the display name). */
  const char *localappdata_folder = nullptr;
  const char *mygames_folder      = nullptr;
  /* Save file extensions, comma-separated (e.g. "ess"). Null = omitted;
   * saves then need another channel (steam_userdata_saves via extra()). */
  const char *save_extensions = nullptr;
  /* Saves subfolder under mygames_folder. Null = omitted. */
  const char *saves_subpath = nullptr;
  /* Profile-specific ini file in the game documents dir. Null = omitted. */
  const char *ini_file = nullptr;
  /* Valid mod subdirs / extensions, comma-separated. Null = omitted. */
  const char *mod_valid_dirs = nullptr;
  const char *mod_valid_exts = nullptr;
  /* Every file the BASE GAME ships (official plugins, DLC archives).
   * Null = omitted - but fill it when the game has any: base-game
   * filtering is a HARD CONSTRAINT. */
  const char *game_native_plugins = nullptr;
  /* Disable-sentinel file name (Isaac "disable.it"). Null = omitted. */
  const char *disable_mechanism = nullptr;
  /* Mod metadata file name (Isaac "metadata.xml"). Null = omitted; set
   * metadata_name_tag / metadata_version_tag with it when the format has
   * named fields. */
  const char *metadata_file        = nullptr;
  const char *metadata_name_tag    = nullptr;
  const char *metadata_version_tag = nullptr;
  /* Mod sources, comma-separated (e.g. "Nexus Mods,Steam"). Null =
   * omitted. */
  const char *download_sources = nullptr;
  /* Per-game masterlist.yaml raw URL. Null = omitted. */
  const char *masterlist_url = nullptr;
  /* Icon URL (SteamGridDB). Null = omitted. */
  const char *game_icon_url = nullptr;
  /* Filesystem behavior + deploy layout. */
  bool case_sensitive        = false; /* hook always registered */
  bool deploy_include_mod_id = false; /* hook registered only when true */

  /* -- Overridable behavior -- */

  /* Tabs. Return empty (the default) to get Mods + Downloads
   * (downloads Nexus-wired when nexus_domain resolves). Override to
   * replace the pair (Skyrim's five-tab layout); strings must be static
   * (literals or static storage). */
  virtual std::vector<Tab> tabs() const { return {}; }

  /* Load-order writer. Default: null = no order_encoding registration
   * (games without a load-order file). Override to return your writer
   * (Skyrim's plugins.txt writer, Isaac's metadata.xml writer). */
  virtual GmmOrderEncodingFnV2 order_encoding() const { return nullptr; }

  /* Escape hatch for anything the descriptor has no field for: tools,
   * game variants, save parsers, categories, workshop / loot / conflict /
   * ignored-files hooks, steam_userdata_saves. Called last, after the
   * descriptor-driven chain. Real examples: SkyrimSpecialEdition.cpp
   * (tool + variants + save parser), TheBindingOfIsaacRebirth.cpp
   * (categories + workshop hooks). For a binary save parser, copy the
   * adapter pattern from SkyrimSESaveParser.cpp (malloc'd GmmSaveDataV2,
   * engine frees the strings, no exceptions cross the ABI). */
  virtual void extra(Ctx &ctx) const { (void)ctx; }

  /* -- Registration -- */

  /* First missing required field, or nullptr when all set. */
  const char *first_missing_required() const {
    if (!nonempty(game_id))
      return "game_id";
    if (!nonempty(display_name))
      return "display_name";
    if (!nonempty(game_binary))
      return "game_binary";
    if (!nonempty(mods_subpath))
      return "mods_subpath";
    return nullptr;
  }

  /* Validate + register the whole plugin. On a missing required field:
   * loud stderr message naming the field, *error filled, NOTHING
   * registered - a half-filled game can never half-load. Returns false
   * only for that refusal (or a null ctx). */
  bool register_all(GmmRegistrationCtxV2 *raw, std::string *error = nullptr) const {
    if (!raw) {
      if (error)
        *error = "null registration context";
      return false;
    }
    const char *missing = first_missing_required();
    if (missing) {
      if (error)
        *error = std::string("missing required field '") + missing +
                 "' - set it in your subclass constructor (required: "
                 "game_id, display_name, game_binary, mods_subpath; see "
                 "BasicGame.hpp)";
      std::fprintf(stderr, "gmm: BasicGame plugin refused to register: %s\n",
                   error ? error->c_str() : "");
      return false;
    }

    Ctx ctx{raw};

    /* Game identity. exe_windows defaults to game_binary. */
    std::string nexus = nexus_domain ? std::string(nexus_domain) : lower_ascii(game_id);
    GmmGameInfo gi;
    gi.game_id        = game_id;
    gi.display_name   = display_name;
    gi.steam_appid    = steam_appid;
    gi.gog_id         = gog_id;
    gi.epic_namespace = epic_namespace;
    gi.nexus_domain   = nexus.c_str();
    gi.exe_windows    = game_binary;
    gi.exe_linux      = exe_linux;
    gi.exe_macos      = exe_macos;
    ctx.game(gi);

    /* Plugin metadata. */
    std::string desc =
        description ? std::string(description)
                    : "Adds basic support for game " + std::string(display_name) + ".";
    ctx.plugin({display_name, author, VERSION, desc.c_str()});

    /* Category - verbatim for game plugins. */
    ctx.category("Game Support");

    /* Tabs: override wins; empty = the default pair. */
    std::string mods_path   = std::string(mods_subpath) + "/";
    std::vector<Tab> custom = tabs();
    if (!custom.empty()) {
      for (const Tab &t : custom)
        ctx.tab(t);
    } else if (nexus.empty()) {
      ctx.tabs(
          {{"mods", "Mods", mods_path.c_str(), "Mod folders managed by GameModManager",
            nullptr, nullptr, nullptr, nullptr, nullptr},
           {"downloads", "Downloads", "downloads/", "Download mods", nullptr, nullptr,
            nullptr, nullptr, nullptr}});
    } else {
      ctx.tabs(
          {{"mods", "Mods", mods_path.c_str(), "Mod folders managed by GameModManager",
            nullptr, nullptr, nullptr, nullptr, nullptr},
           {"downloads", "Downloads", "downloads/", "Download mods", "nxm",
            "nexusmods.com", "nexus", nullptr, nullptr}});
    }

    /* Order encoding: omitted when the game has none. */
    if (GmmOrderEncodingFnV2 fn = order_encoding())
      ctx.order_encoding(fn);

    /* -- Descriptor-driven hooks (each tag verified live; the comment
     * cites its Core consumer) -- */

    /* Detection + identity */
    if (steam_appid != 0) {
      std::string appid = std::to_string(steam_appid);
      ctx.hook("steam_appid", appid.c_str()); /* core.cpp:714-727 VDF match */
    }
    hook_if_set(ctx, "executables", executables ? executables : game_binary);
    hook_if_set(ctx, "game_icon_url", game_icon_url);
    /* localappdata_folder: Core plugin_database.cpp (INI/config dir) */
    hook_if_set(ctx, "localappdata_folder",
                localappdata_folder ? localappdata_folder : display_name);

    /* Mods layout (Core engine/game/detect/mod_scanner.cpp) */
    ctx.hook("mods_subpath", mods_subpath);
    hook_if_set(ctx, "mod_valid_dirs", mod_valid_dirs);
    hook_if_set(ctx, "mod_valid_exts", mod_valid_exts);
    ctx.hook("case_sensitive", case_sensitive ? "true" : "false");
    /* HARD CONSTRAINT: every file the BASE GAME ships must be listed, or
     * base-game content shows up as managed mods. */
    hook_if_set(ctx, "game_native_plugins", game_native_plugins);

    /* Deploy (Core engine/deploy/) - prefix defaults to the mod root. */
    ctx.hook("deploy_prefix", mods_subpath);
    if (deploy_include_mod_id)
      ctx.hook("deploy_include_mod_id", "true");

    /* Mod metadata + load order (Core mod_scanner.cpp) */
    hook_if_set(ctx, "metadata_file", metadata_file);
    hook_if_set(ctx, "metadata_name_tag", metadata_name_tag);
    hook_if_set(ctx, "metadata_version_tag", metadata_version_tag);
    hook_if_set(ctx, "disable_mechanism", disable_mechanism);
    hook_if_set(ctx, "ini_file", ini_file);

    /* Saves (Core engine/game/registry/game_knowledge.cpp) */
    hook_if_set(ctx, "save_extensions", save_extensions);
    hook_if_set(ctx, "saves_subpath", saves_subpath);
    hook_if_set(ctx, "mygames_folder", mygames_folder ? mygames_folder : display_name);

    /* Sources + sorting (Core engine/source/game_catalog.cpp,
     * engine/instance/masterlist_fetch.cpp) */
    hook_if_set(ctx, "download_sources", download_sources);
    hook_if_set(ctx, "masterlist_url", masterlist_url);

    /* Escape hatch - tools, variants, parsers, categories, the rest. */
    extra(ctx);
    return true;
  }

private:
  static bool nonempty(const char *s) { return s && *s; }

  static std::string lower_ascii(const char *s) {
    std::string out = s ? s : "";
    for (char &c : out)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
  }

  /* Register a data hook only when the game actually sets it: an absent
   * tag is better than a placeholder value. */
  static void hook_if_set(Ctx &ctx, const char *tag, const char *value) {
    if (nonempty(value))
      ctx.hook(tag, value);
  }
};

}  // namespace gmm

/* -- Entry points - verbatim, never change. -- */
#define REGISTER_BASIC_GAME(T)                                                         \
  extern "C" void gmm_register_v2(GmmRegistrationCtxV2 *raw) {                         \
    if (!raw)                                                                          \
      return;                                                                          \
    T game;                                                                            \
    if (!game.register_all(raw))                                                       \
      return; /* refused: loud stderr message already printed */                       \
  }                                                                                    \
  extern "C" uint32_t gmm_abi_version(void) {                                          \
    return GMM_ABI_VERSION;                                                            \
  }

#endif /* GMM_BASIC_GAME_HPP */
