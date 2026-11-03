/**
 * Fallout 4 save parser - C ABI adapters.
 *
 * Thin free functions with C linkage matching GmmSaveParserFnV2 /
 * GmmSaveOverlayFnV2. The real work lives in the shared Gamebryo packet
 * (Fallout4SaveGame parses, GamebryoSaveAbi translates to the ABI structs);
 * the adapters only instantiate the right subclass and return 0/1.
 *
 * Returns 0 on any exception (the engine logs the throw via its bridge
 * wrapper) so a corrupted .fos never crashes the saves scan worker.
 */
#pragma once

#include "gmm_abi_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

int fallout4_save_parser(const char *path, const char *game_id, GmmSaveDataV2 *out,
                         void *user_data);

#ifdef __cplusplus
}
#endif
