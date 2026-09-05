/**
 * Skyrim save-parser ABI adapters.
 *
 * Three thin C functions that satisfy
 * GmmSaveParserFnV2 - one per game_id.
 * The packet does the real work; the adapters
 * only:
 *   1. instantiate the right GamebryoSaveGame subclass,
 *   2. deep-copy the
 * parsed SaveInfo into a freshly malloc'd
 *      GmmSaveDataV2 (the engine side frees
 * the strings after copy,
 *      see cb_v2_register_save_parser in Core's
 * plugin_loader.cpp),
 *   3. return 0/1 for the engine's parser bridge.
 *
 * Returns
 * 0 on any exception (the engine logs the throw via its bridge
 * wrapper) so a
 * corrupted .ess never crashes the saves scan worker.
 */

#include "SkyrimSESaveGame.h"
#include "SkyrimSaveGame.h"

#include "gmm_abi_v2.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace
{

// Portable strdup shim - std::strdup is POSIX, not C++; we link
// everywhere we need it.
char* str_dup(const char* s)
{
  if (!s)
    return nullptr;
  std::size_t n = std::strlen(s);
  char* p       = static_cast<char*>(std::malloc(n + 1));
  if (!p)
    return nullptr;
  std::memcpy(p, s, n + 1);
  return p;
}

// strdup with explicit empty->null mapping (the engine treats null and ""
// the same when filling SaveGame, but we keep null to mean "absent").
char* dup_opt(const std::string& s)
{
  if (s.empty())
    return nullptr;
  return str_dup(s.c_str());
}

// Always-non-null strdup: empty input -> empty allocation (the engine
// handles both the same way but this avoids spurious nulls for
// file_path/game_id which are never optional).
char* dup_str(const std::string& s)
{
  return str_dup(s.empty() ? "" : s.c_str());
}

// Copy a string vector into the fixed-size C array in GmmSaveDataV2. The
// engine caps at GMM_SAVE_MAX_PLUGINS, so excess entries are dropped
// (256 already exceeds any real save; the cap is a defensive measure).
void fill_plugin_array(const std::vector<std::string>& src, char** dst, uint32_t& count)
{
  count = 0;
  for (const auto& s : src) {
    if (count >= GMM_SAVE_MAX_PLUGINS)
      break;
    dst[count++] = str_dup(s.c_str());
  }
}

// Translate the packet's SaveInfo into a freshly-allocated GmmSaveDataV2.
// Caller takes ownership of every char* and the top-level struct (the
// engine frees them after copying the strings into a SaveGame).
GmmSaveDataV2* to_abi(const gmm::gamebryo::SaveInfo& info)
{
  GmmSaveDataV2* out =
      static_cast<GmmSaveDataV2*>(std::calloc(1, sizeof(GmmSaveDataV2)));
  if (!out)
    return nullptr;
  out->file_path     = dup_str(info.file_path.string());
  out->game_id       = dup_str(info.game_id);
  out->creation_time = info.creation_time;
  out->pc_name       = dup_opt(info.pc_name);
  out->pc_level      = static_cast<int32_t>(info.pc_level);
  out->pc_location   = dup_opt(info.pc_location);
  out->save_number   = info.save_number;
  fill_plugin_array(info.plugins, out->plugins, out->plugin_count);
  fill_plugin_array(info.light_plugins, out->light_plugins, out->light_plugin_count);
  return out;
}

int parse_with(gmm::gamebryo::SaveInfo (*factory)(const char* path,
                                                  const char* game_id),
               const char* path, const char* game_id, GmmSaveDataV2* out)
{
  if (!path || !game_id || !out)
    return 0;
  try {
    auto info             = factory(path, game_id);
    GmmSaveDataV2* filled = to_abi(info);
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

gmm::gamebryo::SaveInfo make_skyrim_le(const char* path, const char* game_id)
{
  gmm::gamebryo::SkyrimSaveGame s(path, game_id);
  return s.parse();
}

gmm::gamebryo::SaveInfo make_skyrim_se(const char* path, const char* game_id)
{
  gmm::gamebryo::SkyrimSESaveGame s(path, game_id);
  return s.parse();
}

}  // namespace

extern "C" int skyrim_save_parser(const char* path, const char* game_id,
                                  GmmSaveDataV2* out, void* /*user_data*/)
{
  return parse_with(&make_skyrim_le, path, game_id, out);
}

extern "C" int skyrimse_save_parser(const char* path, const char* game_id,
                                    GmmSaveDataV2* out, void* /*user_data*/)
{
  return parse_with(&make_skyrim_se, path, game_id, out);
}

extern "C" int skyrimvr_save_parser(const char* path, const char* game_id,
                                    GmmSaveDataV2* out, void* /*user_data*/)
{
  // VR uses the same SE save format (MO2: SkyrimSESaveGame).
  return parse_with(&make_skyrim_se, path, game_id, out);
}
