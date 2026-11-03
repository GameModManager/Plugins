/**
 * Skyrim save-parser ABI adapters.
 *
 * Three thin C functions with C linkage matching GmmSaveParserFnV2 - one per
 * game_id. The packet does the real work; the adapters only:
 *   1. instantiate the right GamebryoSaveGame subclass,
 *   2. hand the parsed SaveInfo to the shared ABI translator
 *      (GamebryoSaveAbi.h) with Skyrim's ".skse" co-save extension,
 *   3. return 0/1 for the engine's parser bridge.
 *
 * Returns 0 on any exception (the engine logs the throw via its bridge
 * wrapper) so a corrupted .ess never crashes the saves scan worker.
 */

#include "SkyrimSESaveGame.h"
#include "SkyrimSaveGame.h"
#include "GamebryoSaveAbi.h"

#include "gmm_abi_v2.h"

#include <cstdlib>
#include <cstring>

namespace {

int parse_with(gmm::gamebryo::SaveInfo (*factory)(const char *path,
                                                  const char *game_id),
               const char *path, const char *game_id, GmmSaveDataV2 *out) {
  if (!path || !game_id || !out)
    return 0;
  try {
    auto info             = factory(path, game_id);
    GmmSaveDataV2 *filled = gmm::gamebryo::save_info_to_abi(info, "skse");
    if (!filled)
      return 0;
    *out = *filled;
    // The engine reads char* out of the struct and frees them; zero the
    // source so its free() loop on filled (if any) is a no-op.
    std::memset(filled, 0, sizeof(*filled));
    std::free(filled);
    return 1;
  } catch (...) {
    return 0;
  }
}

gmm::gamebryo::SaveInfo make_skyrim_le(const char *path, const char *game_id) {
  gmm::gamebryo::SkyrimSaveGame s(path, game_id);
  return s.parse();
}

gmm::gamebryo::SaveInfo make_skyrim_se(const char *path, const char *game_id) {
  gmm::gamebryo::SkyrimSESaveGame s(path, game_id);
  return s.parse();
}

}  // namespace

extern "C" int skyrim_save_parser(const char *path, const char *game_id,
                                  GmmSaveDataV2 *out, void * /*user_data*/) {
  return parse_with(&make_skyrim_le, path, game_id, out);
}

extern "C" int skyrimse_save_parser(const char *path, const char *game_id,
                                    GmmSaveDataV2 *out, void * /*user_data*/) {
  return parse_with(&make_skyrim_se, path, game_id, out);
}

extern "C" int skyrimvr_save_parser(const char *path, const char *game_id,
                                    GmmSaveDataV2 *out, void * /*user_data*/) {
  // VR uses the same SE save format (MO2: SkyrimSESaveGame).
  return parse_with(&make_skyrim_se, path, game_id, out);
}

extern "C" GmmSaveOverlayV2 *skyrimse_save_overlay(const GmmSaveDataV2 *save,
                                                   void *user_data) {
  return gmm::gamebryo::gamebryo_save_overlay(save, user_data);
}
