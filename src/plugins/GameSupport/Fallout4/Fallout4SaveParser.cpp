/**
 * Fallout 4 save-parser ABI adapters.
 *
 * One C function satisfying GmmSaveParserFnV2. The packet does the real
 * work; the adapter instantiates Fallout4SaveGame and hands the parsed
 * SaveInfo to the shared ABI translator (GamebryoSaveAbi.h) with FO4's
 * ".f4se" co-save extension.
 */

#include "Fallout4SaveGame.h"
#include "GamebryoSaveAbi.h"

#include "gmm_abi_v2.h"

#include <cstdlib>
#include <cstring>

namespace {

gmm::gamebryo::SaveInfo make_fallout4(const char *path, const char *game_id) {
  gmm::gamebryo::Fallout4SaveGame s(path, game_id);
  return s.parse();
}

}  // namespace

extern "C" int fallout4_save_parser(const char *path, const char *game_id,
                                    GmmSaveDataV2 *out, void * /*user_data*/) {
  if (!path || !game_id || !out)
    return 0;
  try {
    auto info             = make_fallout4(path, game_id);
    GmmSaveDataV2 *filled = gmm::gamebryo::save_info_to_abi(info, "f4se");
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
