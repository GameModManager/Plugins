/**
 * Fallout 4 Game Module - third Bethesda ABI consumer
 *
 * Registers:
 * - Identity:
 * steam_appid=377160, nexus_domain=fallout4
 * - Order encoding:
 * plugins.txt writer (same encoding as Skyrim SE - FO4 reads
 * %LOCALAPPDATA%/Fallout4/plugins.txt)
 * - Capabilities: plugins (plugins.txt + BA2s), archives (BA2),
 * downloads
 *   (Nexus)
 * - Tools: LOOT (advisory - sorted plugin list feeds into
 * load order)
 * - Creation Club: Fallout4.ccc in the game root lists force-loaded
 * CC plugins (Core's plugin database reads the "creation_club_file"
 * knowledge key)
 * - Save parser: GMM_Gamebryo packet handles fallout4 (.fos,
 * "FO4_SAVEGAME" magic, uncompressed plugin lists, .f4se co-saves)
 */

#include "plugin.hpp"

#include "Fallout4SaveParser.h"
#include "GamebryoSaveAbi.h"

#include <cstdio>
#include <cstring>

/* -- Identity -- */
static const uint32_t FALLOUT4_APPID = 377160;

/* -- Order encoding: writes plugins.txt (FO4 uses the same format as SE) -- */
static int fallout4_order_encoding(const char *const *ordered_mod_ids, size_t count,
                                   const char *output_path, void *user_data) {
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
extern "C" void gmm_register_v2(GmmRegistrationCtxV2 *raw) {
  if (!raw)
    return;

  gmm::Ctx ctx{raw};

  /* Game identity */
  ctx.game({"Fallout4", "Fallout 4", FALLOUT4_APPID, nullptr, /* gog_id */
            nullptr,                                          /* epic_namespace */
            "fallout4", nullptr, /* exe_windows - detected at runtime */
            nullptr,             /* exe_linux - uses Proton */
            nullptr})            /* exe_macos - not supported */
      /* Plugin metadata */
      .plugin({"Fallout 4", "GameModManager Team", VERSION,
               "Fallout 4 game support (plugins.txt load order, Data/ layout, "
               "BA2 archives, Fallout4.ccc Creation Club)"})
      /* Category */
      .category("Game Support")
      /* Order encoding */
      .order_encoding(fallout4_order_encoding)
      /* Tabs - UI layout */
      .tabs({{"plugins", "Plugins", "Data/",
              "ESP/ESM/ESL plugin load order via plugins.txt", nullptr, nullptr,
              nullptr, "archives", nullptr},
             {"archives", "Archives", "Data/",
              "BA2 archive files loaded by the game engine", nullptr, nullptr, nullptr,
              "downloads", nullptr},
             {"saves", "Saves", "Documents/My Games/Fallout4/Saves/",
              "Save game files (.fos, .f4se)", nullptr, nullptr, nullptr, "downloads",
              "data"},
             {"downloads", "Downloads", "downloads/", "Download mods from Nexus Mods",
              "nxm", "nexusmods.com", "nexus", nullptr, nullptr},
             {"data", "Data", "", "Virtual file system view of the game's Data folder",
              nullptr, nullptr, nullptr, "downloads", "archives"}})
      /* Hooks - game-dependent data */
      .hook("download_sources", "Nexus Mods")
      .hook("mod_counter_label", "Plugins")
      .hook("game_native_plugins",
            "Fallout4.esm,DLCRobot.esm,DLCworkshop01.esm,DLCCoast.esm,"
            "DLCworkshop02.esm,DLCworkshop03.esm,DLCNukaWorld.esm,"
            "DLCUltraHighResolution.esm")
      .hook("mods_subpath", "Data")
      .hook("mod_valid_dirs",
            "interface,meshes,music,scripts,sound,strings,textures,trees,"
            "video,materials,f4se,distantlod,asi,Tools,MCM,distantland,mits,"
            "dllplugins,CalienteTools,shadersfx,aaf")
      .hook("mod_valid_exts", "esp,esm,esl,ba2,modgroups,ini,csg,cdx")
      .hook("case_sensitive", "false")
      .hook("localappdata_folder", "Fallout4")
      .hook("mygames_folder", "Fallout4")
      .hook("executables", "Fallout4.exe,Fallout4Launcher.exe,f4se_loader.exe")
      .hook("steam_appid", "377160")
      .hook("game_icon_url", "https://cdn.cloudflare.steamstatic.com/"
                             "steamcommunity/public/images/apps/377160/"
                             "4779b4257a79b821717aaec10e6ddd5074bb5680.ico")
      /* Creation Club: force-loaded CC plugins listed in the game root's
       * Fallout4.ccc (MO2 GameFallout4::CCPlugins). */
      .hook("creation_club_file", "Fallout4.ccc")
      /* Saves: .fos in My Games/Fallout4/Saves (the engine's
       * "saves_subpath" default is already "Saves"). */
      .hook("save_extensions", "fos")
      /* Tool: LOOT */
      .tool("loot", "advisory")
      /* LOOT identity */
      .hook("loot_game_id", "fallout4")
      .hook("loot_masterlist_repo", "fallout4")
      /* Save parser: shared GMM_Gamebryo packet. Registered under the
         plugin's own game_id ("Fallout4", what instances carry) - the
         Saves scan looks parsers up by instance game_id, so omitting our
         own id silently falls back to the metadata-less stub. */
      .save_parser("Fallout4", &fallout4_save_parser)
      /* Save overlay: per-game rich metadata (Saves tab generic widget). */
      .save_overlay("Fallout4", &gmm::gamebryo::gamebryo_save_overlay);
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void) {
  return GMM_ABI_VERSION;
}

/* -- v2.1 feature bits --
 * Tells the engine which additive GmmSaveDataV2 fields and registration
 * slots this .so actually populates. The Core bridge probes this via dlsym
 * and gates the new-field copy on the matching bit (see plugin_loader.h
 * GMM_FEATURE_*). Set ONLY the bits we really fill; missing bits = the
 * engine treats those fields as absent, matching pre-v2.1 behavior.
 */
extern "C" uint64_t gmm_abi_features(void) {
  // Bit numbers mirror GMM_FEATURE_* in Core's plugin_loader.h:
  //   SAVE_SCREENSHOT=0, SAVE_ALL_FILES=1, SAVE_MEDIUM=2,
  //   SAVE_OVERLAY=3, GAME_VARIANTS=4.
  return (1ull << 0) | (1ull << 1) | (1ull << 3);
}
