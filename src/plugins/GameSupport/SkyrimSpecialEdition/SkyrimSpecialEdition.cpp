/**
 * Skyrim SE Game Module - first real ABI consumer
 *
 * Registers:
 * - Identity:
 * steam_appid=489830, nexus_domain=skyrimspecialedition
 * - Order encoding:
 * plugins.txt writer
 * - Capabilities: plugins (plugins.txt + BSAs), archives (BSAs),
 * downloads
 *   (Nexus)
 * - Tools: LOOT (advisory - sorted plugin list feeds into
 * load order)
 * - Save parser: GMM_Gamebryo packet handles skyrim, skyrimse, skyrimvr
 */

#include "plugin.hpp"

#include "SkyrimSESaveParser.h"

#include <cstdio>
#include <cstring>

/* -- Identity -- */
static const uint32_t SKYRIM_SE_APPID = 489830;

/* -- Order encoding: writes plugins.txt -- */
static int skyrim_order_encoding(const char* const* ordered_mod_ids, size_t count,
                                 const char* output_path, void* user_data)
{
  FILE* f = fopen(output_path, "w");
  if (!f)
    return 0;

  for (size_t i = 0; i < count; i++) {
    fprintf(f, "%s\n", ordered_mod_ids[i]);
  }

  fclose(f);
  return 1;
}

/* -- Registration entry point -- */
extern "C" void gmm_register_v2(GmmRegistrationCtxV2* raw)
{
  if (!raw)
    return;

  gmm::Ctx ctx{raw};

  /* Game identity */
  ctx.game({"SkyrimSpecialEdition", "Skyrim Special Edition", SKYRIM_SE_APPID,
            nullptr,                         /* gog_id */
            nullptr,                         /* epic_namespace */
            "skyrimspecialedition", nullptr, /* exe_windows - detected at runtime */
            nullptr,                         /* exe_linux - uses Proton */
            nullptr})                        /* exe_macos - not supported */
      /* Plugin metadata */
      .plugin({"Skyrim Special Edition", "GameModManager Team", VERSION,
               "Skyrim Special Edition game support (plugins.txt load "
               "order, Data/ layout)"})
      /* Category */
      .category("Game Support")
      /* Order encoding */
      .order_encoding(skyrim_order_encoding)
      /* Tabs - UI layout */
      .tabs({{"plugins", "Plugins", "Data/",
              "ESP/ESM/ESL plugin load order via plugins.txt", nullptr, nullptr,
              nullptr, "archives", nullptr},
             {"archives", "Archives", "Data/",
              "BSA/BA2 archive files loaded by the game engine", nullptr, nullptr,
              nullptr, "downloads", nullptr},
             {"saves", "Saves", "Documents/My Games/Skyrim Special Edition/Saves/",
              "Save game files (.ess, .skse)", nullptr, nullptr, nullptr, "downloads",
              "data"},
             {"downloads", "Downloads", "downloads/", "Download mods from Nexus Mods",
              "nxm", "nexusmods.com", "nexus", nullptr, nullptr},
             {"data", "Data", "", "Virtual file system view of the game's Data folder",
              nullptr, nullptr, nullptr, "downloads", "archives"}})
      /* Hooks - game-dependent data */
      .hook("download_sources", "Nexus Mods,LoversLab")
      .hook("mod_counter_label", "Plugins")
      .hook("game_native_plugins",
            "Skyrim.esm,Update.esm,Dawnguard.esm,HearthFires.esm,"
            "Dragonborn.esm,_ResourcePack.esl")
      .hook("mods_subpath", "Data")
      .hook("mod_valid_dirs",
            "fonts,interface,menus,meshes,music,scripts,shaders,sound,"
            "strings,textures,trees,video,facegen,materials,skse,obse,"
            "mwse,nvse,fose,f4se,distantlod,asi,SkyProc "
            "Patchers,Tools,MCM,icons,bookart,distantland,mits,splash,"
            "dllplugins,CalienteTools,NetScriptFramework,shadersfx")
      .hook("mod_valid_exts", "esp,esm,esl,bsa,ba2,modgroups,ini")
      .hook("case_sensitive", "false")
      .hook("localappdata_folder", "Skyrim Special Edition")
      .hook("mygames_folder", "Skyrim Special Edition")
      .hook("executables", "SkyrimSE.exe,SkyrimSELauncher.exe,skse64_loader.exe")
      .hook("steam_appid", "489830")
      .hook("game_icon_url", "https://cdn2.steamgriddb.com/icon/"
                             "20d749bc05f47d2bd3026ce457dcfd8e/32/64x64.png")
      /* Tool: LOOT */
      .tool("loot", "advisory")
      /* LOOT identity */
      .hook("loot_game_id", "skyrimse")
      .hook("loot_masterlist_repo", "skyrimse")
      /* Save parser: shared GMM_Gamebryo packet, registered for all three
         Gamebryo save game_ids so the Saves tab can read .ess files. */
      .save_parser("skyrim", &skyrim_save_parser)
      .save_parser("skyrimse", &skyrimse_save_parser)
      .save_parser("skyrimvr", &skyrimvr_save_parser)
      /* Save overlay: per-game rich metadata (Saves tab generic widget). */
      .save_overlay("skyrimse", &skyrimse_save_overlay)
      /* Game variants: distinguish Steam / GOG / Epic installs of SkyrimSE. */
      .game_variant("skyrimse", "steam", "Steam")
      .game_variant("skyrimse", "gog", "GOG")
      .game_variant("skyrimse", "epic", "Epic Games Store");
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void)
{
  return GMM_ABI_VERSION;
}

/* -- v2.1 feature bits --
 * Tells the engine which additive GmmSaveDataV2 fields and registration
 * slots this .so actually populates. The Core bridge probes this via dlsym
 * and gates the new-field copy on the matching bit (see plugin_loader.h
 * GMM_FEATURE_*). Set ONLY the bits we really fill; missing bits = the
 * engine treats those fields as absent, matching pre-v2.1 behavior.
 */
extern "C" uint64_t gmm_abi_features(void)
{
  // Bit numbers mirror GMM_FEATURE_* in Core's plugin_loader.h:
  //   SAVE_SCREENSHOT=0, SAVE_ALL_FILES=1, SAVE_MEDIUM=2,
  //   SAVE_OVERLAY=3, GAME_VARIANTS=4.
  return (1ull << 0) | (1ull << 1) | (1ull << 3) | (1ull << 4);
}
