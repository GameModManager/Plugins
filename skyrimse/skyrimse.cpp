/**
 * Skyrim SE Game Module — first real ABI consumer
 *
 * Registers:
 * - Identity: steam_appid=489830, nexus_domain=skyrimspecialedition
 * - Order encoding: plugins.txt writer
 * - Capabilities: plugins (plugins.txt + BSAs), archives (BSAs), downloads (Nexus)
 */

#include "gmm_abi_v1.h"

#include <cstdio>
#include <cstring>

/* ── Identity ── */
static const uint32_t SKYRIM_SE_APPID = 489830;
static const char* NEXUS_DOMAIN = "skyrimspecialedition";

/* ── Order encoding: writes plugins.txt ── */
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

/* ── Registration entry point ── */
extern "C" void gmm_register_v1(GmmRegistrationCtx* ctx) {
    /* Register identity */
    ctx->register_identity(ctx,
        SKYRIM_SE_APPID,
        NULL,           /* gog_id — Skyrim SE not on GOG */
        NULL,           /* epic_namespace — not on Epic */
        NEXUS_DOMAIN,
        NULL,           /* exe_windows — detected at runtime */
        NULL,           /* exe_linux — uses Proton */
        NULL            /* exe_macos — not supported */
    );

    /* Register order encoding hook */
    ctx->register_order_encoding(ctx, skyrim_order_encoding);

    /* Register capabilities — tells the UI which tabs to show */
    ctx->register_capability(ctx,
        "plugins",
        "Plugins",
        "Data/",
        "ESP/ESM/ESL plugin load order via plugins.txt",
        NULL,           /* no protocol handler — plugins come from mods */
        NULL,
        NULL
    );

    ctx->register_capability(ctx,
        "archives",
        "Archives",
        "Data/",
        "BSA/BA2 archive files loaded by the game engine",
        NULL,
        NULL,
        NULL
    );

    ctx->register_capability(ctx,
        "downloads",
        "Downloads",
        "downloads/",
        "Download mods from Nexus Mods",
        "nxm",
        "nexusmods.com",
        "nexus"
    );
}

/* ── Version guard ── */
extern "C" uint32_t gmm_abi_version(void) {
    return GMM_ABI_VERSION;
}
