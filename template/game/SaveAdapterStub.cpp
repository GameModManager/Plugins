/**
 * SaveAdapterStub - OPTIONAL save-parser + overlay adapter pair.
 *
 * OFF BY DEFAULT: the registration lines in GameTemplate.cpp are commented
 * out. Use this file only if your game has a binary save format worth
 * parsing in the Saves tab. If it does not, delete this file, drop it from
 * add_game_plugin, and cover saves with the save_extensions /
 * steam_userdata_saves hooks alone (the Isaac path - Isaac ships no
 * parser).
 *
 * The reusable part (from SkyrimSESaveParser.cpp): thin extern "C"
 * adapters that satisfy GmmSaveParserFnV2 / GmmSaveOverlayFnV2. The
 * adapters only:
 *   1. instantiate your packet's parse entry (FILL in parse_template below),
 *   2. deep-copy the result into a freshly malloc'd GmmSaveDataV2 (the
 *      engine side frees the strings after copy - see
 *      cb_v2_register_save_parser in Core's plugin_loader.cpp),
 *   3. return 0/1 for the engine's parser bridge.
 *
 * Return 0 on any exception so a corrupted save file never crashes the
 * saves scan worker. No exceptions cross the ABI boundary.
 *
 * FILL SUMMARY (search "FILL"):
 *   - SaveInfo fields your packet fills
 *   - parse_template(): call your real packet instead of the empty stub
 *   - build_template_overlay(): the kv rows your Saves tab shows
 */

#include "gmm_abi_v2.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Portable strdup shim - std::strdup is POSIX, not C++.
char *str_dup(const char *s) {
  if (!s)
    return nullptr;
  std::size_t n = std::strlen(s);
  char *p       = static_cast<char *>(std::malloc(n + 1));
  if (!p)
    return nullptr;
  std::memcpy(p, s, n + 1);
  return p;
}

// strdup with explicit empty->null mapping (null means "absent").
char *dup_opt(const std::string &s) {
  if (s.empty())
    return nullptr;
  return str_dup(s.c_str());
}

// Always-non-null strdup: empty input -> empty allocation (file_path and
// game_id are never optional).
char *dup_str(const std::string &s) {
  return str_dup(s.empty() ? "" : s.c_str());
}

// Copy a string vector into the fixed-size C array in GmmSaveDataV2. The
// engine caps at GMM_SAVE_MAX_PLUGINS; excess entries are dropped.
void fill_plugin_array(const std::vector<std::string> &src, char **dst,
                       uint32_t &count) {
  count = 0;
  for (const auto &s : src) {
    if (count >= GMM_SAVE_MAX_PLUGINS)
      break;
    dst[count++] = str_dup(s.c_str());
  }
}

// FILL: the parsed-save shape your packet returns. Keep only the fields
// your game actually has; the adapter below maps them onto GmmSaveDataV2.
struct SaveInfo {
  std::filesystem::path file_path;
  std::string game_id;
  std::string pc_name;
  std::string pc_location;
  int32_t pc_level      = 0;
  uint32_t save_number  = 0;
  int64_t creation_time = 0;
  std::vector<std::string> plugins;
};

// Translate the packet's SaveInfo into a freshly-allocated GmmSaveDataV2.
// Caller takes ownership of every char* and the top-level struct.
GmmSaveDataV2 *to_abi(const SaveInfo &info) {
  GmmSaveDataV2 *out =
      static_cast<GmmSaveDataV2 *>(std::calloc(1, sizeof(GmmSaveDataV2)));
  if (!out)
    return nullptr;
  out->file_path     = dup_str(info.file_path.string());
  out->game_id       = dup_str(info.game_id);
  out->creation_time = info.creation_time;
  out->pc_name       = dup_opt(info.pc_name);
  out->pc_level      = info.pc_level;
  out->pc_location   = dup_opt(info.pc_location);
  out->save_number   = info.save_number;
  fill_plugin_array(info.plugins, out->plugins, out->plugin_count);

  // FILL (optional, v2.1): screenshot_rgba / medium_plugins / all_files.
  // These are gated by the gmm_abi_features() bitmask (see GameTemplate.cpp
  // header notes); leave them zeroed (calloc) until you fill them AND
  // export the matching feature bits.
  return out;
}

int parse_with(SaveInfo (*factory)(const char *path, const char *game_id),
               const char *path, const char *game_id, GmmSaveDataV2 *out) {
  if (!path || !game_id || !out)
    return 0;
  try {
    auto info             = factory(path, game_id);
    GmmSaveDataV2 *filled = to_abi(info);
    if (!filled)
      return 0;
    *out = *filled;
    // The engine reads char* out of the struct and frees them; zero the
    // source so a double free is impossible.
    std::memset(filled, 0, sizeof(*filled));
    std::free(filled);
    return 1;
  } catch (...) {
    return 0;
  }
}

// FILL: replace the body with a call into your real save-parsing packet
// (the Skyrim pattern: gmm::gamebryo::SkyrimSESaveGame s(path, game_id);
// return s.parse();). Until filled this returns an empty SaveInfo, the
// adapter reports "not parsed" (0), and Core falls back to its
// metadata-less stub - safe to ship, useless until you fill it.
SaveInfo parse_template(const char *path, const char *game_id) {
  SaveInfo info;
  info.file_path = path;
  info.game_id   = game_id;
  return info;
}

// -- Save overlay (OPTIONAL) --
// Builds a data-driven GmmSaveOverlayV2 shown by the Saves tab's generic
// widget. The engine owns the malloc'd struct + strings after we return.
// FILL: the kv rows your game's save detail view should show beyond the
// default metadata (name/level/location) the engine already renders.
GmmSaveOverlayV2 *build_template_overlay(const GmmSaveDataV2 *save,
                                         void * /*user_data*/) {
  if (!save)
    return nullptr;
  char num[32];
  std::snprintf(num, sizeof(num), "%u", save->save_number);
  const char *keys[1]   = {"Save #"};
  const char *values[1] = {num};

  GmmSaveOverlayV2 *ov =
      static_cast<GmmSaveOverlayV2 *>(std::calloc(1, sizeof(GmmSaveOverlayV2)));
  if (!ov)
    return nullptr;
  ov->title = str_dup("Save details");
  if (save->pc_name)
    ov->subtitle = str_dup(save->pc_name);
  const int n   = 1;  // FILL: row count
  ov->kv_keys   = static_cast<char **>(std::malloc(sizeof(char *) * n));
  ov->kv_values = static_cast<char **>(std::malloc(sizeof(char *) * n));
  if (!ov->kv_keys || !ov->kv_values) {
    std::free(ov->kv_keys);
    std::free(ov->kv_values);
    std::free(ov->title);
    std::free(ov->subtitle);
    std::free(ov);
    return nullptr;
  }
  for (int i = 0; i < n; ++i) {
    ov->kv_keys[i]   = str_dup(keys[i]);
    ov->kv_values[i] = str_dup(values[i]);
  }
  ov->kv_count = n;
  return ov;
}

}  // namespace

extern "C" int template_game_save_parser(const char *path, const char *game_id,
                                         GmmSaveDataV2 *out, void * /*user_data*/) {
  return parse_with(&parse_template, path, game_id, out);
}

extern "C" GmmSaveOverlayV2 *template_game_save_overlay(const GmmSaveDataV2 *save,
                                                        void *user_data) {
  return build_template_overlay(save, user_data);
}
