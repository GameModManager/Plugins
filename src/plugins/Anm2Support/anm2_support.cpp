/**
 * Anm2Support Plugin — Animation parser for .anm2 files (v2 ABI)
 *
 * Non-game-specific plugin. Registers as a plugin (no game identity — it is a
 * "File Support" plugin, not a game) and would normally provide an animation
 * parser for .anm2 files (Isaac animation format).
 *
 * NOTE: The v2 ABI is a clean break from v1 and does not expose an
 * animation-parser registration point — v1's register_animation_parser and the
 * GmmAnimationDataC / GmmAnimationLayerC / GmmAnimationFrameC types were removed.
 * The .anm2 parser implementation therefore cannot be registered under v2 and is
 * disabled pending v2 engine support for animation parsing. This plugin
 * currently registers only its plugin metadata.
 *
 * Build: shared library (MODULE), no Qt, no engine linkage — uses only the
 * stable C ABI from gmm_abi_v2.h.
 */

#include "gmm_abi_v2.h"

#include <cstdio>

extern "C" {

uint32_t gmm_abi_version() { return GMM_ABI_VERSION; }

void gmm_register_v2(GmmRegistrationCtxV2* ctx) {
    if (!ctx)
        return;

    /* -- Plugin metadata for the Plugins settings tab --
     * Anm2Support is a "File Support" plugin, not a game. We deliberately do
     * NOT call register_game: doing so would set game_support = true and make
     * it appear in the game selector, which is wrong for a file-format plugin.
     * It is discovered and listed via register_plugin instead. */
    GmmPluginInfo info{};
    info.name = "ANM2 Support";
    info.author = "GameModManager Team";
    info.version = "1.0.0";
    info.description = "ANM2 animation file support (.anm2 parsing and preview)";
    ctx->register_plugin(ctx, info);

    /* -- Animation parser --
     * The v2 ABI has no register_animation_parser equivalent yet, so the .anm2
     * parser cannot be registered. Kept disabled until v2 gains an animation
     * parser interface. */
}

} /* extern "C" */
