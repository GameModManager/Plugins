/**
 * GameTemplate - copy-and-fill skeleton for a new game-support plugin.
 *
 * HOW TO USE THIS TEMPLATE
 *
 * 1. Copy template/game/ to src/plugins/GameSupport/<YourGame>/.
 * 2. Work through every FILL marker below (template/game/README.md has the
 *    full walkthrough; template/README.md has the replace-vs-verbatim list).
 * 3. Wire the CMake block from template/cmake_lifecycle.md.
 * 4. Build + run the template guard tests (see cmake_lifecycle.md).
 *
 * RULES THIS FILE ALREADY FOLLOWS (do not re-derive them):
 * - Stable C ABI only: includes plugin.hpp (ABI repo), never Core headers.
 * - Fluent gmm::Ctx registration, the style Skyrim SE and Isaac use.
 * - The .so target name MUST equal the game_id in game() below: Core
 *   defaults an instance's game_id to the .so stem before register_game
 *   runs (Core plugin_loader.cpp), so a mismatched rename splits identity.
 * - Every hook tag below is verified live: Core reads it (each comment
 *   cites the Core consumer). Dead tags are excluded on purpose - do not
 *   add a tag without grepping Core for a reader (tools/check_hook_tags.sh
 *   enforces this for the template; keep it green for your copy too).
 * - No gmm_abi_features export: omitting it is the safe v2.0 baseline.
 *   Only add it when your save parser fills v2.1 GmmSaveDataV2 fields,
 *   and mirror Core's GMM_FEATURE_* bit numbering exactly.
 * - Base-game filtering is a HARD CONSTRAINT: game_native_plugins below
 *   must list every file the game itself ships, or base-game content
 *   shows up as managed mods.
 *
 * OPTIONAL pieces (rich-plugin route), clearly marked:
 * - order_encoding writer below: generic one-per-line writer; replace the
 *   body with your game's real load-order format, or delete the writer
 *   and its .order_encoding() line if your game has no load-order file.
 * - SaveAdapterStub.cpp in this directory: a compiling save-parser +
 *   overlay adapter pair. Its registration lines are COMMENTED OUT below
 *   (off by default). Uncomment them only after you fill the stub's
 *   factory with your real packet call; delete the file and the
 *   add_game_plugin source line if your game needs no binary save
 *   parsing (the Isaac path: saves via save_extensions hooks alone).
 */

#include "plugin.hpp"

#include <cstdio>

/* -- Identity - FILL every field -- */
/* steam_appid drives game detection: Core matches it against the user's
 * Steam library.vdf (Core/src/app/core.cpp:714-727). 0 = not on Steam. */
static const uint32_t STEAM_APPID = 0;  // FILL: Steam appid, or 0

/* -- Order encoding (OPTIONAL) - generic one-per-line writer --
 * FILL: replace the body with your game's real load-order format.
 * Skyrim writes plugins.txt lines (SkyrimSpecialEdition.cpp); Isaac writes
 * metadata.xml rows (TheBindingOfIsaacRebirth.cpp). Delete this writer and
 * the .order_encoding() line in the chain if your game has no such file.
 * Return 1 on success, 0 on failure (ABI convention). */
static int template_order_encoding(const char *const *ordered_mod_ids, size_t count,
                                   const char *output_path, void *user_data) {
  FILE *f = fopen(output_path, "w");
  if (!f)
    return 0;

  for (size_t i = 0; i < count; i++) {
    fprintf(f, "%s\n", ordered_mod_ids[i]);  // FILL: your format
  }

  fclose(f);
  (void)user_data;
  return 1;
}

/* -- Registration entry point -- */
extern "C" void gmm_register_v2(GmmRegistrationCtxV2 *raw) {
  if (!raw)
    return;

  gmm::Ctx ctx{raw};

  /* Game identity - FILL all fields.
   * game_id MUST equal the CMake target name (the .so stem). exe_windows /
   * exe_linux / exe_macos: nullptr means "detected at runtime" or "not
   * supported" - see SkyrimSpecialEdition.cpp for the per-OS pattern. */
  ctx.game({"GameTemplate",       /* FILL: game_id == CMake target name */
            "Game Template",      /* FILL: display name */
            STEAM_APPID, nullptr, /* FILL: gog_id, or nullptr */
            nullptr,              /* FILL: epic_namespace, or nullptr */
            "gametemplate",       /* FILL: nexus_domain, or nullptr */
            nullptr,              /* FILL: exe_windows, or nullptr */
            nullptr,              /* FILL: exe_linux, or nullptr */
            nullptr})             /* FILL: exe_macos, or nullptr */
      /* Plugin metadata - FILL name + description. VERSION is the
       * GMM_VERSION define from the build (Core's project version); leave
       * the macro, never hardcode a number. */
      .plugin({"Game Template",       /* FILL */
               "GameModManager Team", /* FILL: author */
               VERSION,
               "GameTemplate game support " /* FILL: description */
               "(one-line summary of the game's mod layout)"})
      /* Category - verbatim for game plugins. */
      .category("Game Support")
      /* OPTIONAL: mod categories (Isaac registers 22, Skyrim none).
       * Uncomment + FILL only if your game's mods benefit from a category
       * tree; ids must not collide with MO2's defaults (max 58) - Isaac
       * starts at 1000. */
      // .categories({
      //     {1000, "Items", 0},
      //     {1001, "Items/Child", 1000},
      // })
      /* Order encoding (OPTIONAL, see the writer above). */
      .order_encoding(template_order_encoding)
      /* Tabs - FILL. Capabilities Core renders today: plugins, archives,
       * saves, downloads, data, mods, conflicts. data_path is relative to
       * the game dir; protocol_handler/website_domain wire the downloads
       * tab to a source (see the "nxm"/"nexusmods.com" pair in Skyrim).
       * insert_before/insert_after order tabs (capability names or
       * nullptr). */
      .tabs({{"mods", "Mods", "mods/", /* FILL: data_path + description */
              "Mod folders managed by GameModManager", nullptr, nullptr, nullptr,
              nullptr, nullptr},
             {"downloads", "Downloads", "downloads/",
              "Download mods", /* FILL: protocol trio if Nexus applies */
              nullptr, nullptr, nullptr, nullptr, nullptr}})
      /* -- Hooks: game-dependent data. EVERY tag below is read by Core
       * (consumer cited per tag). FILL the value, or delete the line if
       * the aspect does not apply to your game - an unfilled tag with a
       * placeholder value is worse than an absent one. -- */

      /* Detection + identity */
      .hook("steam_appid", "0")  /* FILL; Core core.cpp:714-727 VDF match */
      .hook("executables", "")   /* FILL: comma-separated exe names */
      .hook("game_icon_url", "") /* FILL; Core engine/instance/game_icons.h */
      .hook("localappdata_folder",
            "") /* FILL; Core plugin_database.cpp (INI/config dir) */

      /* Mods layout (Core engine/game/detect/mod_scanner.cpp) */
      .hook("mods_subpath", "")        /* FILL: e.g. "Data" */
      .hook("mod_scan_subpath", "")    /* FILL or delete: scan root override */
      .hook("mod_valid_dirs", "")      /* FILL: comma-separated valid subdirs */
      .hook("mod_valid_exts", "")      /* FILL: e.g. "esp,esm,bsa" */
      .hook("case_sensitive", "false") /* FILL per game FS behavior */
      .hook("ignored_files", "")       /* FILL: VCS/metadata junk to ignore */
      .hook("ignored_dirs", "")        /* FILL or delete */
      /* HARD CONSTRAINT: every file the BASE GAME ships (official
       * plugins, DLC) must be listed here so it never shows as a managed
       * mod. See knowledgebase/cpp/agent-rules.md "Base Game Filtering". */
      .hook("game_native_plugins", "")  /* FILL: base-game plugin files */
      .hook("creation_club_file", "cc") /* FILL: CC filename prefix, or delete */

      /* Deploy (Core engine/deploy/) */
      .hook("deploy_prefix", "")              /* FILL: deploy root under the game dir */
      .hook("deploy_include_mod_id", "false") /* FILL: nest deploys under <mod_id>/ ? */
      /* FILL or delete: symlink | overlayfs | direct (Core
       * engine/deploy/deploy_utils.h). Omitting lets Core default. */
      // .hook("deploy_strategy", "symlink")
      .hook("game_mods_dir", "") /* FILL: real game-side mods dir */

      /* Mod metadata + load order (Core mod_scanner.cpp) */
      .hook("metadata_file", "")        /* FILL: e.g. "metadata.xml" */
      .hook("metadata_name_tag", "")    /* FILL */
      .hook("metadata_version_tag", "") /* FILL */
      .hook("metadata_tag_element", "") /* FILL or delete */
      .hook("priority_prefix_re", "")   /* FILL: order prefix regex */
      .hook("priority_format", "")      /* FILL: e.g. "%03d " */
      .hook("disable_mechanism", "")    /* FILL: sentinel file name */
      /* FILL or delete: "true" defers disable-sentinel writes to
       * deploy/launch (Core cli/headless_launcher.cpp). */
      // .hook("delayed_disable", "true")
      .hook("separator_suffix", "") /* FILL or delete */
      .hook("ini_file", "")         /* FILL or delete */
      .hook("uses_merged", "false") /* FILL: does the game merge mod files? */

      /* Conflicts (Core ui/controllers/overwrite_controller.cpp,
       * mod_actions.cpp) */
      .hook("conflict_scan_dirs", "")           /* FILL: dirs to scan, or delete */
      .hook("conflict_order_reversed", "false") /* FILL: later-wins games set "true" */

      /* Steam Workshop (Core mod_scanner.cpp) - delete both if the game
       * has no Workshop presence. */
      .hook("workshop_id_pattern", "")     /* FILL: id extraction regex */
      .hook("workshop_tag_categories", "") /* FILL: tag->category JSON */

      /* Saves (Core engine/game/registry/game_knowledge.cpp) */
      .hook("save_extensions", "") /* FILL: e.g. "ess" or "dat" */
      .hook("saves_subpath", "")   /* FILL or delete */
      /* FILL or delete: "true" when saves live in Steam userdata, not
       * Documents (Isaac path). */
      // .hook("steam_userdata_saves", "remote")
      .hook("mygames_folder", "") /* FILL: Documents subfolder, or delete */
      /* FILL or delete: per-user variant + parent overrides (Core
       * game_knowledge.cpp handles mygames_folder_ / mygames_parent). */
      // .hook("mygames_folder_", "")
      // .hook("mygames_parent", "")
      /* Only if Core implements a fast-scan reader for your save format
       * (Core engine/game/save_fast_scan.h; today only "gamebryo-tesv").
       * Delete otherwise - a made-up value is a dead hook. */
      // .hook("save_fast_format", "")

      /* Sources + sorting (Core engine/source/game_catalog.cpp,
       * engine/instance/masterlist_fetch.cpp) */
      .hook("download_sources", "") /* FILL: e.g. "Nexus Mods,Steam" */
      .hook("masterlist_url",
            "") /* FILL: per-game masterlist.yaml raw URL, or delete */

      /* LOOT integration (Core ui/controllers/mod_list_controller.cpp) -
       * delete both if the game has no LOOT masterlist. */
      // .hook("loot_game_id", "")
      // .hook("loot_masterlist_repo", "")

      /* OPTIONAL: advisory tool registration (Skyrim registers LOOT).
       * Uncomment + FILL only with a real tool id Core knows. */
      // .tool("loot", "advisory")

      /* OPTIONAL: game variants (Steam/GOG/Epic installs of the same
       * game). Register under your game_id AND any short alias, exactly
       * as Skyrim does. Uncomment + FILL if needed. */
      // .game_variant("GameTemplate", "steam", "Steam")
      // .game_variant("GameTemplate", "gog", "GOG")

      /* OPTIONAL: save parser + overlay (SaveAdapterStub.cpp). OFF by
       * default: fill the stub's factory with your real packet call,
       * uncomment these lines, and add the stub to add_game_plugin.
       * Register under your own game_id FIRST (instances carry it; the
       * Saves scan looks parsers up by instance game_id - registering
       * only aliases silently falls back to the metadata-less stub),
       * then any aliases. */
      // .save_parser("GameTemplate", &template_game_save_parser)
      // .save_overlay("GameTemplate", &template_game_save_overlay)
      ;
}

/* -- Version guard - verbatim, never change. -- */
extern "C" uint32_t gmm_abi_version(void) {
  return GMM_ABI_VERSION;
}
