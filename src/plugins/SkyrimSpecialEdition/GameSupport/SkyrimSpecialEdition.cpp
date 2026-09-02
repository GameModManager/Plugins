/**
 * Skyrim SE Game Module — first real ABI consumer
 *
 * Registers:
 * - Identity: steam_appid=489830, nexus_domain=skyrimspecialedition
 * - Order encoding: plugins.txt writer
 * - Capabilities: plugins (plugins.txt + BSAs), archives (BSAs), downloads
 * (Nexus)
 * - Tools: LOOT (advisory — sorted plugin list feeds into load order)
 */

#include "gmm_abi_v2.h"

#include <cstdio>
#include <cstring>

/* -- Identity -- */
static const uint32_t SKYRIM_SE_APPID = 489830;
static const char *NEXUS_DOMAIN = "skyrimspecialedition";

/* -- Order encoding: writes plugins.txt -- */
static int skyrim_order_encoding(const char *const *ordered_mod_ids,
                                 size_t count, const char *output_path,
                                 void *user_data) {
  FILE *f = fopen(output_path, "w");
  if (!f)
    return 0;

  for (size_t i = 0; i < count; i++) {
    fprintf(f, "%s\n", ordered_mod_ids[i]);
  }

  fclose(f);
  return 1;
}

/* -- Registration entry point -- */
extern "C" void gmm_register_v2(GmmRegistrationCtxV2 *ctx) {
  if (!ctx)
    return;

  /* Register game identity */
  GmmGameInfo game{};
  game.game_id = "SkyrimSpecialEdition";
  game.display_name = "Skyrim Special Edition";
  game.steam_appid = SKYRIM_SE_APPID;
  game.gog_id = nullptr;
  game.epic_namespace = nullptr;
  game.nexus_domain = NEXUS_DOMAIN;
  game.exe_windows = nullptr; /* detected at runtime */
  game.exe_linux = nullptr;   /* uses Proton */
  game.exe_macos = nullptr;   /* not supported */
  ctx->register_game(ctx, game);

  /* Plugin metadata for the Plugins settings tab */
  GmmPluginInfo info{};
  info.name = "Skyrim Special Edition";
  info.author = "GameModManager Team";
  info.version = VERSION;
  info.description = "Skyrim Special Edition game support (plugins.txt load "
                     "order, Data/ layout)";
  ctx->register_plugin(ctx, info);

  /* Category for the Plugins settings tab */
  ctx->register_category(ctx, "Game Support");

  /* Register order encoding hook */
  ctx->register_order_encoding(ctx, skyrim_order_encoding, nullptr);

  /* Register tabs — tells the UI which tabs to show and their order */
  ctx->register_tab(ctx, "plugins", "Plugins", "Data/",
                    "ESP/ESM/ESL plugin load order via plugins.txt",
                    nullptr, /* no protocol handler — plugins come from mods */
                    nullptr, nullptr,
                    "archives", /* insert_before: Plugins before Archives */
                    nullptr);

  ctx->register_tab(ctx, "archives", "Archives", "Data/",
                    "BSA/BA2 archive files loaded by the game engine", nullptr,
                    nullptr, nullptr,
                    "downloads", /* insert_before: Archives before Downloads */
                    nullptr);

  ctx->register_tab(ctx, "saves", "Saves",
                    "Documents/My Games/Skyrim Special Edition/Saves/",
                    "Save game files (.ess, .skse)", nullptr, nullptr, nullptr,
                    "downloads", /* insert_before: Saves before Downloads */
                    "data"       /* insert_after: Saves after Data */
  );

  ctx->register_tab(ctx, "downloads", "Downloads", "downloads/",
                    "Download mods from Nexus Mods", "nxm", "nexusmods.com",
                    "nexus", nullptr, /* insert_before: none — goes last */
                    nullptr);

  /* Position "Data" between Archives and Downloads */
  ctx->register_tab(ctx, "data", "Data", "",
                    "Virtual file system view of the game's Data folder", nullptr,
                    nullptr, nullptr,
                    "downloads", /* insert_before: Data before Downloads */
                    "archives"   /* insert_after: Data after Archives */
  );

  /* Download sources — for status bar display */
  ctx->register_hook(ctx, "download_sources", "Nexus Mods,LoversLab", nullptr, 0, nullptr);

  /* Mod counter label — "Plugins" for Skyrim (ESM/ESP/ESL load order) */
  ctx->register_hook(ctx, "mod_counter_label", "Plugins", nullptr, 0, nullptr);

  /* Game-native plugins — vanilla ESMs that ship with the game.
   * These appear as unmanaged mods in the list (cannot be removed/reordered).
   * _ResourcePack.esl is Bethesda's resource pack, part of the same fixed
   * band (MO2 lists it as "Creation Club: _ResourcePack"). */
  ctx->register_hook(ctx, "game_native_plugins",
                     "Skyrim.esm,Update.esm,Dawnguard.esm,HearthFires.esm,"
                     "Dragonborn.esm,_ResourcePack.esl",
                     nullptr, 0, nullptr);

  /* Mods subpath — Skyrim stores its plugins in Data/ relative to the game root
   */
  ctx->register_hook(ctx, "mods_subpath", "Data", nullptr, 0, nullptr);

  /* Content-validity markers (MO2's GamebryoModDataChecker allow-sets):
   * top-level folder names and file extensions that count as real game data.
   * A mod folder with none of these - and no meta.ini - is flagged "No valid
   * game data" in the mod list (MO2's FLAG_INVALID). Mirrors
   * gamebryomoddatachecker.cpp possibleFolderNames()/possibleFileExtensions().
   */
  ctx->register_hook(ctx, "mod_valid_dirs",
                     "fonts,interface,menus,meshes,music,scripts,shaders,sound,"
                     "strings,textures,trees,video,facegen,materials,skse,obse,"
                     "mwse,nvse,fose,f4se,distantlod,asi,SkyProc "
                     "Patchers,Tools,MCM,icons,bookart,distantland,mits,splash,"
                     "dllplugins,CalienteTools,NetScriptFramework,shadersfx",
                     nullptr, 0, nullptr);
  ctx->register_hook(ctx, "mod_valid_exts", "esp,esm,esl,bsa,ba2,modgroups,ini",
                     nullptr, 0, nullptr);

  /* Case-insensitive game (Windows filesystem): the deploy layer must merge
   * CI-equal paths (e.g. Meshes/ vs meshes/) into one canonical casing when
   * building the overlay staging tree, or the case-sensitive mount splits a
   * mod across two dirs and the game reads a half-populated mod. */
  ctx->register_hook(ctx, "case_sensitive", "false", nullptr, 0, nullptr);

  /* Plugin support: %LOCALAPPDATA%/<localappdata_folder>/Plugins.txt lives
   * under the Proton prefix's users/<user>/AppData/Local. Required for
   * write_plugins_txt_for_launch() to resolve the target; without it the
   * launch-time Plugins.txt write is skipped and the game keeps the stale
   * file (mods never get the '*' enable prefix). */
  ctx->register_hook(ctx, "localappdata_folder", "Skyrim Special Edition", NULL,
                     0, NULL);

  /* My Games folder: <prefix>/drive_c/users/<user>/Documents/My Games/
   * <mygames_folder>. Used by the open-folder menu (MO2's
   * documentsDirectory() equivalent) — MO2 resolves it per-game via
   * determineMyGamesPath(gameName()). */
  ctx->register_hook(ctx, "mygames_folder", "Skyrim Special Edition", NULL, 0,
                     NULL);

  /* Executables — relative to the game install root */
  ctx->register_hook(ctx, "executables",
                     "SkyrimSE.exe,SkyrimSELauncher.exe,skse64_loader.exe",
                     nullptr, 0, nullptr);

  /* Steam appid — used by ProtonRuntime to find the right Proton version.
   * Also used by platform detection to find the game's Steam install dir. */
  ctx->register_hook(ctx, "steam_appid", "489830", nullptr, 0, nullptr);

  /* Declared game icon — SteamGridDB CDN asset. The engine downloads it into
   * its global icon cache (~/.local/share/GameModManager/cache/icons) on
   * first use and enforces the UI's target size when rendering. */
  ctx->register_hook(ctx, "game_icon_url",
                     "https://cdn2.steamgriddb.com/icon/"
                     "20d749bc05f47d2bd3026ce457dcfd8e/32/64x64.png",
                     nullptr, 0, nullptr);

  /* Register LOOT as an advisory tool — output feeds into load order */
  ctx->register_tool(
      ctx, "loot", "advisory",
      nullptr, /* invoke_fn — detected and invoked by engine at runtime */
      nullptr  /* user_data */
  );

  /* LOOT identity — LOOT game slug + masterlist repo (PLAN.md §7.1). The
   * engine LootSorter builds its gmm_lootcli request from these. */
  ctx->register_hook(ctx, "loot_game_id", "skyrimse", nullptr, 0, nullptr);

  ctx->register_hook(ctx, "loot_masterlist_repo", "skyrimse", nullptr, 0, nullptr);
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void) { return GMM_ABI_VERSION; }
