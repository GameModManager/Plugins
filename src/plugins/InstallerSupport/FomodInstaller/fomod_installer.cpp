/**
 * FOMOD Installer Plugin — thin orchestrator for FOMOD mod archives (v2 ABI)
 *
 * Non-game-specific plugin. Claims the "Fomod" pipeline stage via a wildcard
 * stage claim so it applies to every game.
 *
 * NOTE: The v2 ABI is a clean break from v1 and removed the v1 host_ui
 * fomod_wizard bridge, so this plugin can no longer delegate FOMOD
 * detection / wizard execution to the host. The stage handler therefore acts as
 * a pass-through (returns 1) until v2 provides a FOMOD install interface. The
 * plugin metadata, wildcard stage claim, and settings are still registered.
 *
 * Build: shared library (MODULE), no Qt, no engine linkage — uses only the
 * stable C ABI from gmm_abi_v2.h.
 */

#include "gmm_abi_v2.h"

#include <cstring>

/* --------------------------------------------------------------------------
 * Stage handler — called on the pipeline thread for each mod install.
 *
 * v2 has no host UI wizard bridge, so we cannot run the FOMOD wizard here.
 * We return 1 (pass through) so the pipeline continues unchanged.
 * ------------------------------------------------------------------------ */
static int fomod_stage_handler(void* mod,
                               void* instance,
                               void* conflicts,
                               void* profile,
                               void* user_data) {
    (void)mod;
    (void)instance;
    (void)conflicts;
    (void)profile;
    (void)user_data;
    return 1;  /* pass-through: v2 has no host UI wizard bridge */
}

/* --------------------------------------------------------------------------
 * Plugin registration entry point
 * ------------------------------------------------------------------------ */
extern "C" {

uint32_t gmm_abi_version() {
    return GMM_ABI_VERSION;
}

void gmm_register_v2(GmmRegistrationCtxV2* ctx) {
    if (!ctx)
        return;

    /* -- Plugin metadata -- */
    GmmPluginInfo info{};
    info.name = "FOMOD Installer";
    info.author = "GameModManager Team";
    info.version = "1.0.0";
    info.description =
        "FOMOD installer — detects and installs FOMOD archives via the wizard";
    ctx->register_plugin(ctx, info);

    /* -- Claim the "Fomod" pipeline stage (wildcard — applies to all games) --
     *
     * Uses register_wildcard_stage_claim with game_id=NULL (wildcard) so this
     * plugin handles FOMOD for every game. Game-specific plugins can override
     * at equal or higher priority if needed.
     *
     * Priority 10: wins over the core baseline (0), leaves room for
     * game-specific overrides at higher priority. */
    if (ctx->register_wildcard_stage_claim) {
        ctx->register_wildcard_stage_claim(ctx,
            nullptr,            /* game_id — NULL = wildcard (all games) */
            "Fomod",            /* stage name */
            fomod_stage_handler,
            10);                /* priority */
    }

    /* -- Settings (key-value pairs) -- */
    if (ctx->register_settings) {
        static const char* keys[] = {
            "Restore previous choices",
            "Show FOMOD images",
        };
        static const char* values[] = {
            "1",  // default: enabled
            "1",  // default: enabled
        };
        ctx->register_settings(ctx, keys, values, 2);
    }
}

}  /* extern "C" */
