/**
 * Skyrim SE Game Module — first real ABI consumer
 *
 * Registers:
 * - Identity: steam_appid=489830, nexus_domain=skyrimspecialedition
 * - Order encoding: plugins.txt writer
 * - Capabilities: plugins (plugins.txt + BSAs), archives (BSAs), downloads (Nexus)
 * - Tools: LOOT (advisory — sorted plugin list feeds into load order)
 */

#include "gmm_abi_v1.h"

#include <cstdio>
#include <cstring>

/* -- Identity -- */
static const uint32_t SKYRIM_SE_APPID = 489830;
static const char* NEXUS_DOMAIN = "skyrimspecialedition";

/* -- Order encoding: writes plugins.txt -- */
static int skyrim_order_encoding(const char* const* ordered_mod_ids,
                                 size_t count,
                                 const char* output_path,
                                 void* user_data) {
    FILE* f = fopen(output_path, "w");
    if (!f) return 0;

    for (size_t i = 0; i < count; i++) {
        fprintf(f, "%s\n", ordered_mod_ids[i]);
    }

    fclose(f);
    return 1;
}

/* -- Registration entry point -- */
extern "C" void gmm_register_v1(GmmRegistrationCtx* ctx) {
    /* Register identity */
    ctx->register_identity(ctx,
        SKYRIM_SE_APPID,
        NULL,           /* gog_id — Skyrim SE not on GOG */
        NULL,           /* epic_namespace — not on Epic */
        NEXUS_DOMAIN,
        "Skyrim Special Edition",
        NULL,           /* exe_windows — detected at runtime */
        NULL,           /* exe_linux — uses Proton */
        NULL            /* exe_macos — not supported */
    );

    /* Optional metadata for the Plugins settings tab */
    if (ctx->register_meta) {
        ctx->register_meta(ctx,
            "GameModManager Team",
            "0.1.0",
            "Skyrim Special Edition game support (plugins.txt load order, Data/ layout)");
    }
    if (ctx->register_category) {
        ctx->register_category(ctx, "Game Support");
    }

    /* Register order encoding hook */
    ctx->register_order_encoding(ctx, skyrim_order_encoding);

    /* Register tabs — tells the UI which tabs to show and their order */
    ctx->register_tab(ctx,
        "plugins",
        "Plugins",
        "Data/",
        "ESP/ESM/ESL plugin load order via plugins.txt",
        NULL,           /* no protocol handler — plugins come from mods */
        NULL,
        NULL,
        "archives",     /* insert_before: Plugins before Archives */
        NULL
    );

    ctx->register_tab(ctx,
        "archives",
        "Archives",
        "Data/",
        "BSA/BA2 archive files loaded by the game engine",
        NULL,
        NULL,
        NULL,
        "downloads",    /* insert_before: Archives before Downloads */
        NULL
    );

    ctx->register_tab(ctx,
        "saves",
        "Saves",
        "Documents/My Games/Skyrim Special Edition/Saves/",
        "Save game files (.ess, .skse)",
        NULL,
        NULL,
        NULL,
        "downloads",    /* insert_before: Saves before Downloads */
        "data"          /* insert_after: Saves after Data */
    );

    ctx->register_tab(ctx,
        "downloads",
        "Downloads",
        "downloads/",
        "Download mods from Nexus Mods",
        "nxm",
        "nexusmods.com",
        "nexus",
        NULL,           /* insert_before: none — goes last */
        NULL
    );

    /* Position "Data" between Archives and Downloads */
    ctx->register_tab(ctx,
        "data",
        "Data",
        "",
        "Virtual file system view of the game's Data folder",
        NULL,
        NULL,
        NULL,
        "downloads",    /* insert_before: Data before Downloads */
        "archives"      /* insert_after: Data after Archives */
    );

    /* Download sources — for status bar display */
    ctx->register_hook(ctx,
        "download_sources",
        "Nexus",
        NULL, 0, NULL);

    /* Mod counter label — "Plugins" for Skyrim (ESM/ESP/ESL load order) */
    ctx->register_hook(ctx,
        "mod_counter_label",
        "Plugins",
        NULL, 0, NULL);

    /* Game-native plugins — vanilla ESMs that ship with the game.
     * These appear as unmanaged mods in the list (cannot be removed/reordered). */
    ctx->register_hook(ctx,
        "game_native_plugins",
        "Skyrim.esm,Update.esm,Dawnguard.esm,HearthFires.esm,Dragonborn.esm",
        NULL, 0, NULL);

    /* Mods subpath — Skyrim stores its plugins in Data/ relative to the game root */
    ctx->register_hook(ctx,
        "mods_subpath",
        "Data",
        NULL, 0, NULL);

    /* Plugin support: %LOCALAPPDATA%/<localappdata_folder>/Plugins.txt lives
     * under the Proton prefix's users/<user>/AppData/Local. Required for
     * write_plugins_txt_for_launch() to resolve the target; without it the
     * launch-time Plugins.txt write is skipped and the game keeps the stale
     * file (mods never get the '*' enable prefix). */
    ctx->register_hook(ctx,
        "localappdata_folder",
        "Skyrim Special Edition",
        NULL, 0, NULL);

    /* Executables — relative to the game install root */
    ctx->register_hook(ctx,
        "executables",
        "SkyrimSE.exe,SkyrimSELauncher.exe,skse64_loader.exe",
        NULL, 0, NULL);

    /* Steam appid — used by ProtonRuntime to find the right Proton version.
     * Also used by platform detection to find the game's Steam install dir. */
    ctx->register_hook(ctx,
        "steam_appid",
        "489830",
        NULL, 0, NULL);

    /* Register LOOT as an advisory tool — output feeds into load order */
    ctx->register_tool(ctx,
        "loot",
        "advisory",
        NULL,           /* invoke_fn — detected and invoked by engine at runtime */
        NULL            /* user_data */
    );
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void) {
    return GMM_ABI_VERSION;
}
