#include "GamebryoSaveAbi.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace gmm::gamebryo {

namespace {

  // Portable strdup shim - std::strdup is POSIX, not C++; we link
  // everywhere we need it.
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

  // strdup with explicit empty->null mapping (the engine treats null and ""
  // the same when filling SaveGame, but we keep null to mean "absent").
  char *dup_opt(const std::string &s) {
    if (s.empty())
      return nullptr;
    return str_dup(s.c_str());
  }

  // Always-non-null strdup: empty input -> empty allocation (the engine
  // handles both the same way but this avoids spurious nulls for
  // file_path/game_id which are never optional).
  char *dup_str(const std::string &s) {
    return str_dup(s.empty() ? "" : s.c_str());
  }

  // Copy a string vector into the fixed-size C array in GmmSaveDataV2. The
  // engine caps at GMM_SAVE_MAX_PLUGINS, so excess entries are dropped
  // (256 already exceeds any real save; the cap is a defensive measure).
  void fill_plugin_array(const std::vector<std::string> &src, char **dst,
                         uint32_t &count) {
    count = 0;
    for (const auto &s : src) {
      if (count >= GMM_SAVE_MAX_PLUGINS)
        break;
      dst[count++] = str_dup(s.c_str());
    }
  }

}  // namespace

GmmSaveDataV2 *save_info_to_abi(const SaveInfo &info, const char *co_save_ext) {
  GmmSaveDataV2 *out =
      static_cast<GmmSaveDataV2 *>(std::calloc(1, sizeof(GmmSaveDataV2)));
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
  // The packet stores either w*h*4 (SE save game version >= 12, and every
  // FO4 save) or w*h*3 (LE) raw pixels. The ABI requires RGBA, so LE has to
  // be expanded with an opaque alpha. Empty or mismatched -> leave nulled
  // (calloc).
  const int w = info.screenshot_width;
  const int h = info.screenshot_height;
  if (w > 0 && h > 0) {
    const size_t wh  = static_cast<size_t>(w) * h;
    const size_t src = info.screenshot.size();
    uint8_t *rgba    = nullptr;
    size_t bytes     = 0;
    if (src == wh * 4) {
      // Already RGBA (SE / FO4). Direct malloc + memcpy.
      bytes = wh * 4;
      rgba  = static_cast<uint8_t *>(std::malloc(bytes));
      if (rgba)
        std::memcpy(rgba, info.screenshot.data(), bytes);
    } else if (src == wh * 3) {
      // LE: RGB -> RGBA, opaque alpha. Single pass.
      bytes = wh * 4;
      rgba  = static_cast<uint8_t *>(std::malloc(bytes));
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
  // Skyrim-family and FO4 have no medium plugins; leave the array zeroed.
  // (Bethesda ESL-flagged ESMs as SAVE MEDIUM exist only on Starfield.)

  // -- v2.1 all_files (SAVE_ALL_FILES) --
  // The save itself plus its script-extender co-save when one exists
  // (SKSE/F4SE - the caller names the extension).
  std::vector<std::string> files;
  files.push_back(info.file_path.string());
  if (co_save_ext && *co_save_ext && info.file_path.has_extension()) {
    auto co = info.file_path;
    co.replace_extension(std::string(".") + co_save_ext);
    std::error_code ec;
    if (std::filesystem::exists(co, ec))
      files.push_back(co.string());
  }
  if (!files.empty()) {
    char **arr = static_cast<char **>(std::malloc(sizeof(char *) * files.size()));
    if (arr) {
      uint32_t kept = 0;
      for (const auto &s : files) {
        if (char *d = str_dup(s.c_str()))
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

GmmSaveOverlayV2 *gamebryo_save_overlay(const GmmSaveDataV2 *save,
                                        void * /*user_data*/) {
  if (!save)
    return nullptr;
  // Build kv rows: save number, plugin count, light plugin count.
  char num[32];
  std::snprintf(num, sizeof(num), "%u", save->save_number);
  char pc[32];
  std::snprintf(pc, sizeof(pc), "%u", save->plugin_count);
  char lpc[32];
  std::snprintf(lpc, sizeof(lpc), "%u", save->light_plugin_count);
  const char *keys[3]   = {"Save #", "Plugins", "Light plugins"};
  const char *values[3] = {num, pc, lpc};

  GmmSaveOverlayV2 *ov =
      static_cast<GmmSaveOverlayV2 *>(std::calloc(1, sizeof(GmmSaveOverlayV2)));
  if (!ov)
    return nullptr;
  ov->title = str_dup("Save details");
  if (save->pc_name)
    ov->subtitle = str_dup(save->pc_name);
  ov->kv_keys   = static_cast<char **>(std::malloc(sizeof(char *) * 3));
  ov->kv_values = static_cast<char **>(std::malloc(sizeof(char *) * 3));
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

}  // namespace gmm::gamebryo
