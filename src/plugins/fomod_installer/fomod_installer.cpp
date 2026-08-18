/**
 * FOMOD Installer Plugin — thin orchestrator for FOMOD mod archives
 *
 * Non-game-specific plugin. Claims the "Fomod" pipeline stage via a wildcard
 * stage claim so it applies to every game. The actual FOMOD detection, XML
 * parsing, and file installation are done host-side by the engine's FomodStage
 * through the P1.4 host_ui.fomod_wizard bridge.
 *
 * This plugin:
 *   1. Claims the "Fomod" stage at priority 10 (wins over the core baseline
 *      at 0, leaves room for game-specific overrides).
 *   2. Delegates to the host's FomodStage via host_ui.fomod_wizard.
 *   3. Registers user-facing settings ("Restore previous choices",
 *      "Show FOMOD images") as key-value pairs via register_settings.
 *   4. Reports identity under the "Installer" category in the Plugins tab.
 *
 * Build: shared library (MODULE), no Qt, no engine linkage — uses only the
 * stable C ABI from gmm_abi_v1.h.
 */

#include "gmm_abi_v1.h"

#include <cstring>

/* -- Host UI bridge pointer, cached during registration -- */
static GmmFomodWizardFn s_fomod_wizard = nullptr;

/* --------------------------------------------------------------------------
 * Stage handler — called on the pipeline thread for each mod install.
 *
 * The handler delegates to the host's FomodStage through the
 * host_ui.fomod_wizard bridge. The host detects fomod/, parses
 * ModuleConfig.xml, shows the wizard dialog, and applies the chosen options
 * to the staging directory. The outcome comes back as JSON.
 *
 * Outcome keys:
 *   "installed"  — success, pipeline continues
 *   "not_fomod"  — not a FOMOD archive, pass through (pipeline continues)
 *   "canceled"   — user aborted wizard, pipeline stops
 *   "failed"     — install error, pipeline stops
 * ------------------------------------------------------------------------ */
static int fomod_stage_handler(GmmModHandle mod,
                               GmmInstanceHandle instance,
                               GmmConflictIndexHandle conflicts,
                               GmmProfileHandle profile,
                               void* user_data) {
    (void)instance;
    (void)conflicts;
    (void)profile;
    (void)user_data;

    if (!s_fomod_wizard) return 1;  /* No wizard wired — pass through */

    char json_buf[4096];
    const int ok = s_fomod_wizard(mod, json_buf, sizeof(json_buf));
    if (!ok) return 0;

    /* Interpret the outcome. The host's FomodStage already applied the
     * choices to the staging directory when it returned ok=1. We only need
     * to propagate success/failure to the pipeline. */
    if (std::strstr(json_buf, "\"outcome\":\"not_fomod\"")) return 1;  /* pass through */
    if (std::strstr(json_buf, "\"outcome\":\"canceled\""))  return 0;  /* user abort */
    if (std::strstr(json_buf, "\"outcome\":\"failed\""))    return 0;  /* install error */
    /* "installed" or anything else — success */
    return ok;
}

/* --------------------------------------------------------------------------
 * Plugin registration entry point
 * ------------------------------------------------------------------------ */
extern "C" {

uint32_t gmm_abi_version() {
    return GMM_ABI_VERSION;
}

void gmm_register_v1(GmmRegistrationCtx* ctx) {
    /* Save the host UI bridge for use in the stage handler.
     * The function pointer is stable for the process lifetime. */
    if (ctx->host_ui.fomod_wizard) {
        s_fomod_wizard = ctx->host_ui.fomod_wizard;
    }

    /* -- Metadata -- */
    if (ctx->register_meta) {
        ctx->register_meta(ctx,
            "GameModManager Team",                       /* author */
            "1.0.0",                                     /* version */
            "FOMOD installer — detects and installs FOMOD archives via the wizard");
    }

    /* -- Category for the Plugins settings tab -- */
    if (ctx->register_category) {
        ctx->register_category(ctx, "Installer");
    }

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
