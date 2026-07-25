/**
 * Isaac Game Module — second ABI consumer
 *
 * Validates that the ABI generalizes beyond Skyrim SE.
 * Isaac uses metadata.xml for load order, structurally opposite to plugins.txt.
 *
 * Capabilities: mods (metadata.xml), downloads (Nexus)
 * No "plugins" or "archives" tabs — Isaac doesn't use those concepts.
 */

#include "gmm_abi_v1.h"

#include <cstdio>
#include <cstring>

/* ── Identity ── */
static const char* NEXUS_DOMAIN = "isaac";

/* ── Order encoding: writes metadata.xml (Isaac's load order format) ── */
static int isaac_order_encoding(const char* const* ordered_mod_ids,
                                size_t count,
                                const char* output_path,
                                void* user_data) {
    FILE* f = fopen(output_path, "w");
    if (!f) return 0;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<mods>\n");
    for (size_t i = 0; i < count; i++) {
        fprintf(f, "  <mod id=\"%s\" priority=\"%zu\" />\n", ordered_mod_ids[i], i);
    }
    fprintf(f, "</mods>\n");

    fclose(f);
    return 1;
}

/* ── Registration entry point ── */
extern "C" void gmm_register_v1(GmmRegistrationCtx* ctx) {
    /* Register identity */
    ctx->register_identity(ctx,
        0,              /* steam_appid — Isaac not on Steam */
        NULL,           /* gog_id */
        NULL,           /* epic_namespace */
        NEXUS_DOMAIN,
        NULL,           /* exe_windows */
        NULL,           /* exe_linux */
        NULL            /* exe_macos */
    );

    /* Register order encoding hook — metadata.xml format */
    ctx->register_order_encoding(ctx, isaac_order_encoding);

    /* Register capabilities — Isaac only shows Data + Downloads */
    ctx->register_capability(ctx,
        "downloads",
        "Downloads",
        "mods/",
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
