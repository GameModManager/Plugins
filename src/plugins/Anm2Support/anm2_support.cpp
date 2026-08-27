/**
 * Anm2Support Plugin — Animation parser for .anm2 files
 *
 * Non-game-specific plugin. Registers as a "File Support" category plugin
 * and provides an animation parser for .anm2 files (Isaac animation format).
 *
 * The .anm2 format is XML-based with the following structure:
 *   <Animation fps="30" Width="400" Height="300">
 *     <Frame Delay="100">
 *       <Layer Id="0" Visible="true">
 *         <Position X="0" Y="0"/>
 *         <Crop X="0" Y="0" Width="100" Height="100"/>
 *       </Layer>
 *     </Frame>
 *   </Animation>
 *
 * Build: shared library (MODULE), no Qt, no engine linkage — uses only the
 * stable C ABI from gmm_abi_v1.h.
 */

#include "gmm_abi_v1.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/* --------------------------------------------------------------------------
 * Simple XML parser for .anm2 files
 * ------------------------------------------------------------------------ */

/* Helper: extract attribute value from an XML tag string */
static bool extract_attr(const std::string &tag, const char *attr_name,
                         std::string &out_value) {
  std::string search = std::string(attr_name) + "=\"";
  size_t pos = tag.find(search);
  if (pos == std::string::npos) {
    /* Try without quotes (some XML variants) */
    search = std::string(attr_name) + "=";
    pos = tag.find(search);
    if (pos == std::string::npos)
      return false;
    pos += search.length();
    size_t end = tag.find_first_of(" \t\n\r/>", pos);
    if (end == std::string::npos)
      end = tag.length();
    out_value = tag.substr(pos, end - pos);
    return true;
  }
  pos += search.length();
  size_t end = tag.find('"', pos);
  if (end == std::string::npos)
    return false;
  out_value = tag.substr(pos, end - pos);
  return true;
}

/* Helper: convert string to float with fallback */
static float to_float(const std::string &s, float fallback = 0.0f) {
  try {
    return std::stof(s);
  } catch (...) {
    return fallback;
  }
}

/* Helper: convert string to int32 with fallback */
static int32_t to_int32(const std::string &s, int32_t fallback = 0) {
  try {
    return static_cast<int32_t>(std::stoi(s));
  } catch (...) {
    return fallback;
  }
}

/* Helper: read entire file into a string */
static std::string read_file(const char *path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return "";
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

/* Helper: find a line containing a substring */
static bool find_line(const std::string &content, const char *substr,
                      std::string &line_out) {
  size_t pos = content.find(substr);
  if (pos == std::string::npos)
    return false;
  size_t line_start = content.rfind('\n', pos);
  if (line_start == std::string::npos)
    line_start = 0;
  else
    line_start++;
  size_t line_end = content.find('\n', pos);
  if (line_end == std::string::npos)
    line_end = content.length();
  line_out = content.substr(line_start, line_end - line_start);
  return true;
}

/* --------------------------------------------------------------------------
 * .anm2 parser implementation
 * ------------------------------------------------------------------------ */
static int parse_anm2(const char *file_path, const char *base_dir,
                      GmmAnimationDataC *out, void *user_data) {
  (void)base_dir;
  (void)user_data;

  if (!out)
    return 0;

  /* Read file content */
  std::string content = read_file(file_path);
  if (content.empty())
    return 0;

  /* Parse root <Animation> element */
  std::string anim_line;
  if (!find_line(content, "<Animation", anim_line))
    return 0;

  std::string fps_str, width_str, height_str;
  extract_attr(anim_line, "fps", fps_str);
  extract_attr(anim_line, "Width", width_str);
  extract_attr(anim_line, "Height", height_str);

  out->fps = to_float(fps_str, 30.0f);
  out->canvas_width = to_int32(width_str, 400);
  out->canvas_height = to_int32(height_str, 300);

  /* Parse frames */
  std::vector<GmmAnimationFrameC> frames;
  size_t pos = 0;

  while (true) {
    /* Find next <Frame> tag */
    pos = content.find("<Frame", pos);
    if (pos == std::string::npos)
      break;

    /* Extract frame line */
    size_t line_start = content.rfind('\n', pos);
    if (line_start == std::string::npos)
      line_start = 0;
    else
      line_start++;
    size_t line_end = content.find('\n', pos);
    if (line_end == std::string::npos)
      line_end = content.length();
    std::string frame_line = content.substr(line_start, line_end - line_start);

    /* Extract Delay attribute */
    std::string delay_str;
    extract_attr(frame_line, "Delay", delay_str);
    float delay_ms = to_float(delay_str, 100.0f);

    /* Find closing </Frame> tag */
    size_t frame_end = content.find("</Frame>", pos);
    if (frame_end == std::string::npos)
      break;

    /* Extract frame content */
    std::string frame_content = content.substr(pos, frame_end - pos);

    /* Parse layers within this frame */
    std::vector<GmmAnimationLayerC> layers;
    size_t layer_pos = 0;

    while (true) {
      layer_pos = frame_content.find("<Layer", layer_pos);
      if (layer_pos == std::string::npos)
        break;

      /* Extract layer line */
      size_t l_line_start = frame_content.rfind('\n', layer_pos);
      if (l_line_start == std::string::npos)
        l_line_start = 0;
      else
        l_line_start++;
      size_t l_line_end = frame_content.find('\n', layer_pos);
      if (l_line_end == std::string::npos)
        l_line_end = frame_content.length();
      std::string layer_line =
          frame_content.substr(l_line_start, l_line_end - l_line_start);

      /* Extract Id attribute */
      std::string id_str;
      extract_attr(layer_line, "Id", id_str);
      int32_t layer_id = to_int32(id_str, 0);

      /* Find Position and Crop tags within this layer's scope */
      size_t layer_scope_start = layer_pos;
      size_t layer_scope_end = frame_content.find("</Layer>", layer_pos);
      if (layer_scope_end == std::string::npos)
        layer_scope_end = frame_content.length();
      std::string layer_scope = frame_content.substr(
          layer_scope_start, layer_scope_end - layer_scope_start);

      /* Parse Position */
      int32_t x = 0, y = 0;
      std::string pos_line;
      if (find_line(layer_scope, "<Position", pos_line)) {
        std::string x_str, y_str;
        extract_attr(pos_line, "X", x_str);
        extract_attr(pos_line, "Y", y_str);
        x = to_int32(x_str, 0);
        y = to_int32(y_str, 0);
      }

      /* Parse Crop (used as width/height) */
      int32_t width = 0, height = 0;
      std::string crop_line;
      if (find_line(layer_scope, "<Crop", crop_line)) {
        std::string w_str, h_str;
        extract_attr(crop_line, "Width", w_str);
        extract_attr(crop_line, "Height", h_str);
        width = to_int32(w_str, 0);
        height = to_int32(h_str, 0);
      }

      /* Create layer (pixel data not available from XML alone) */
      GmmAnimationLayerC layer = {};
      layer.x = x;
      layer.y = y;
      layer.width = width;
      layer.height = height;
      layer.rgba_pixels = nullptr;
      layer.pixel_count = 0;
      layers.push_back(layer);

      layer_pos = layer_scope_end;
    }

    /* Create frame */
    GmmAnimationFrameC frame = {};
    frame.delay_ms = delay_ms;
    if (!layers.empty()) {
      frame.layers = new GmmAnimationLayerC[layers.size()];
      std::memcpy(frame.layers, layers.data(),
                  layers.size() * sizeof(GmmAnimationLayerC));
    } else {
      frame.layers = nullptr;
    }
    frame.layer_count = layers.size();
    frames.push_back(frame);

    pos = frame_end + 8; /* Skip past </Frame> */
  }

  /* Copy frames to output */
  if (!frames.empty()) {
    out->frames = new GmmAnimationFrameC[frames.size()];
    std::memcpy(out->frames, frames.data(),
                frames.size() * sizeof(GmmAnimationFrameC));
  } else {
    out->frames = nullptr;
  }
  out->frame_count = frames.size();

  return 1;
}

/* --------------------------------------------------------------------------
 * Plugin registration entry point
 * ------------------------------------------------------------------------ */
extern "C" {

uint32_t gmm_abi_version() { return GMM_ABI_VERSION; }

void gmm_register_v1(GmmRegistrationCtx *ctx) {
  /* -- Identity: set display name to "ANM2" -- */
  if (ctx->register_identity) {
    ctx->register_identity(ctx,
                           0, /* steam_appid — N/A for file-format plugins */
                           nullptr,  /* gog_id */
                           nullptr,  /* epic_namespace */
                           nullptr,  /* nexus_domain */
                           "ANM2",   /* display_name */
                           nullptr,  /* exe_windows */
                           nullptr,  /* exe_linux */
                           nullptr); /* exe_macos */
  }

  /* -- Metadata for the Plugins settings tab -- */
  if (ctx->register_meta) {
    ctx->register_meta(
        ctx, "GameModManager Team", "1.0.0",
        "ANM2 animation file support (.anm2 parsing and preview)");
  }

  /* -- Category for the Plugins settings tab -- */
  if (ctx->register_category) {
    ctx->register_category(ctx, "File Support");
  }

  /* -- Register animation parser for .anm2 files --
   * game_id = NULL (non-game-specific, applies to all games)
   * file_extension = "anm2"
   * priority = 0 (default) */
  if (ctx->register_animation_parser) {
    ctx->register_animation_parser(
        ctx, nullptr, /* game_id — NULL = non-game-specific */
        "anm2",       /* file extension */
        parse_anm2,   /* parser function */
        0,            /* priority */
        nullptr);     /* user_data */
  }
}

} /* extern "C" */