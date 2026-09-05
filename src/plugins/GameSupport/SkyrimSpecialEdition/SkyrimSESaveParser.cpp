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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
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

  // -- v2.1 screenshot (SAVE_SCREENSHOT) --
  // The packet stores either w*h*4 (SE save game version >= 12) or w*h*3
  // (LE) raw pixels. The ABI requires RGBA, so LE has to be expanded
  // with an opaque alpha. Empty or mismatched -> leave nulled (calloc).
  const int w = info.screenshot_width;
  const int h = info.screenshot_height;
  if (w > 0 && h > 0) {
    const size_t wh  = static_cast<size_t>(w) * h;
    const size_t src = info.screenshot.size();
    uint8_t* rgba    = nullptr;
    size_t bytes     = 0;
    if (src == wh * 4) {
      // Already RGBA (SE). Direct malloc + memcpy.
      bytes = wh * 4;
      rgba  = static_cast<uint8_t*>(std::malloc(bytes));
      if (rgba)
        std::memcpy(rgba, info.screenshot.data(), bytes);
    } else if (src == wh * 3) {
      // LE: RGB -> RGBA, opaque alpha. Single pass.
      bytes = wh * 4;
      rgba  = static_cast<uint8_t*>(std::malloc(bytes));
      if (rgba) {
        for (size_t i = 0; i < wh; ++i) {
          rgba[i * 4 + 0] = info.screenshot[i * 3 + 0];
          rgba[i * 4 + 1] = info.screenshot[i * 3 + 1];
          rgba[i * 4 + 2] = info.screenshot[i * 3 + 2];
          rgba[i * 4 + 3] = 0xFF;
        }
      }
    }
    if (rgba) {
      out->screenshot_rgba   = rgba;
      out->screenshot_size   = bytes;
      out->screenshot_width  = w;
      out->screenshot_height = h;
    }
  }

  // -- v2.1 medium plugins (SAVE_MEDIUM) --
  // Skyrim-family has no medium plugins; leave the array zeroed.
  // (Bethesda ESL-flagged ESMs exist only on FO4/Starfield.)

  // -- v2.1 all_files (SAVE_ALL_FILES) --
  // The save itself plus its script-extender co-save when one exists
  // (SKSE/F4SE/etc. - "skse" is the default; FO4 would override via
  // script_extender_extension()). The adapter doesn't keep a Gamebryo
  // instance, so we replicate has_script_extender_file() inline.
  std::vector<std::string> files;
  files.push_back(info.file_path.string());
  if (info.file_path.has_extension()) {
    auto co = info.file_path;
    co.replace_extension(".skse");
    std::error_code ec;
    if (std::filesystem::exists(co, ec))
      files.push_back(co.string());
  }
  if (!files.empty()) {
    char** arr = static_cast<char**>(std::malloc(sizeof(char*) * files.size()));
    if (arr) {
      uint32_t kept = 0;
      for (const auto& s : files) {
        if (char* d = str_dup(s.c_str()))
          arr[kept++] = d;
      }
      if (kept > 0) {
        out->all_files       = arr;
        out->all_files_count = kept;
      } else {
        std::free(arr);
      }
    }
  }

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

// -- Save overlay (SAVE_OVERLAY) --
// Builds a data-driven GmmSaveOverlayV2 from the parsed save. The engine
// owns the malloc'd struct + strings after we return. Adds a few summary
// rows beyond the default metadata the engine already shows
// (name/level/location).
GmmSaveOverlayV2* build_skyrimse_overlay(const GmmSaveDataV2* save, void* /*user_data*/)
{
  if (!save)
    return nullptr;
  // Build kv rows: save number, plugin count, light plugin count.
  char num[32];
  std::snprintf(num, sizeof(num), "%u", save->save_number);
  char pc[32];
  std::snprintf(pc, sizeof(pc), "%u", save->plugin_count);
  char lpc[32];
  std::snprintf(lpc, sizeof(lpc), "%u", save->light_plugin_count);
  const char* keys[3]   = {"Save #", "Plugins", "Light plugins"};
  const char* values[3] = {num, pc, lpc};

  GmmSaveOverlayV2* ov =
      static_cast<GmmSaveOverlayV2*>(std::calloc(1, sizeof(GmmSaveOverlayV2)));
  if (!ov)
    return nullptr;
  ov->title = str_dup("Save details");
  if (save->pc_name)
    ov->subtitle = str_dup(save->pc_name);
  ov->kv_keys   = static_cast<char**>(std::malloc(sizeof(char*) * 3));
  ov->kv_values = static_cast<char**>(std::malloc(sizeof(char*) * 3));
  if (!ov->kv_keys || !ov->kv_values) {
    std::free(ov->kv_keys);
    std::free(ov->kv_values);
    std::free(ov->title);
    std::free(ov->subtitle);
    std::free(ov);
    return nullptr;
  }
  for (int i = 0; i < 3; ++i) {
    ov->kv_keys[i]   = str_dup(keys[i]);
    ov->kv_values[i] = str_dup(values[i]);
  }
  ov->kv_count = 3;
  return ov;
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

extern "C" GmmSaveOverlayV2* skyrimse_save_overlay(const GmmSaveDataV2* save,
                                                   void* user_data)
{
  return build_skyrimse_overlay(save, user_data);
}
