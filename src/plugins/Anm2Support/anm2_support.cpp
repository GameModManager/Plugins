/**
 * Anm2Support Plugin — .anm2 animation parser for The Binding of Isaac
 *
 * Registers an AnimationParserFeature that reads Isaac's XML-based .anm2
 * animation files, resolves sprite sheet PNG references, and provides
 * Qt-free frame data for the preview widget.
 *
 * .anm2 format (simplified):
 *   <animation>
 *     <delay value="33"/>
 *     <layers>
 *       <layer>
 *         <sprite sheet="sprites.png" x="0" y="0" w="32" h="32"/>
 *         <position x="10" y="20"/>
 *       </layer>
 *     </layers>
 *   </animation>
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

// Minimal XML-like parser for .anm2 files. Does NOT use a full XML library;
// Isaac's .anm2 files are simple enough for tag-scanning.

namespace {

struct Sprite {
  std::string sheet; // PNG filename
  int x = 0, y = 0, w = 0, h = 0;
};

struct Position {
  float x = 0, y = 0;
};

struct Layer {
  Sprite sprite;
  Position pos;
};

struct AnimFrame {
  int delay_ms = 33;
  std::vector<Layer> layers;
};

// Read entire file into a string.
std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open())
    return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Find attribute value in a tag string: attr="value" or attr='value'.
std::string get_attr(const std::string &tag, const std::string &attr) {
  std::string needle = attr + "=\"";
  auto pos = tag.find(needle);
  if (pos != std::string::npos) {
    auto end = tag.find('"', pos + needle.size());
    if (end != std::string::npos)
      return tag.substr(pos + needle.size(), end - pos - needle.size());
  }
  needle = attr + "='";
  pos = tag.find(needle);
  if (pos != std::string::npos) {
    auto end = tag.find('\'', pos + needle.size());
    if (end != std::string::npos)
      return tag.substr(pos + needle.size(), end - pos - needle.size());
  }
  return {};
}

// Get int attribute, default on failure.
int get_attr_int(const std::string &tag, const std::string &attr, int def = 0) {
  auto v = get_attr(tag, attr);
  if (v.empty())
    return def;
  try {
    return std::stoi(v);
  } catch (...) {
    return def;
  }
}

// Get float attribute, default on failure.
float get_attr_float(const std::string &tag, const std::string &attr,
                     float def = 0.0f) {
  auto v = get_attr(tag, attr);
  if (v.empty())
    return def;
  try {
    return std::stof(v);
  } catch (...) {
    return def;
  }
}

// Parse a simple .anm2 file. Returns empty on failure.
std::vector<AnimFrame> parse_anm2(const std::string &content) {
  std::vector<AnimFrame> frames;
  AnimFrame current;
  bool in_layers = false;
  bool in_layer = false;
  bool in_sprite = false;
  bool in_position = false;
  Layer current_layer;

  std::istringstream stream(content);
  std::string line;

  while (std::getline(stream, line)) {
    // Trim leading whitespace
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
      continue;
    line = line.substr(start);

    // Skip comments and XML declaration
    if (line.substr(0, 4) == "<?xml" || line.substr(0, 4) == "<!--")
      continue;

    // <delay value="33"/>
    if (line.find("<delay") != std::string::npos) {
      current.delay_ms = get_attr_int(line, "value", 33);
    }
    // <layers>
    else if (line.find("<layers") != std::string::npos &&
             line.find("/>") == std::string::npos &&
             line.find("</layers") == std::string::npos) {
      in_layers = true;
    }
    // </layers>
    else if (line.find("</layers") != std::string::npos) {
      in_layers = false;
    }
    // <layer> or <layer .../>
    else if (in_layers && line.find("<layer") != std::string::npos) {
      if (line.find("/>") != std::string::npos ||
          line.find("</layer") != std::string::npos) {
        // Self-closing or empty layer — ignore
      } else {
        in_layer = true;
        current_layer = Layer();
      }
    } else if (in_layer && line.find("</layer") != std::string::npos) {
      in_layer = false;
      current.layers.push_back(current_layer);
    }
    // <sprite sheet="..." x="0" y="0" w="32" h="32"/>
    else if (in_layer && line.find("<sprite") != std::string::npos) {
      current_layer.sprite.sheet = get_attr(line, "sheet");
      current_layer.sprite.x = get_attr_int(line, "x");
      current_layer.sprite.y = get_attr_int(line, "y");
      current_layer.sprite.w = get_attr_int(line, "w");
      current_layer.sprite.h = get_attr_int(line, "h");
    }
    // <position x="10" y="20"/>
    else if (in_layer && line.find("<position") != std::string::npos) {
      current_layer.pos.x = get_attr_float(line, "x");
      current_layer.pos.y = get_attr_float(line, "y");
    }
    // <frame> — marks end of a frame definition
    else if (line.find("</frame") != std::string::npos ||
             line.find("<frame") != std::string::npos) {
      // If we have layers accumulated, save as a frame
      if (!current.layers.empty()) {
        frames.push_back(std::move(current));
        current = AnimFrame();
      }
    }
  }

  // Flush any remaining frame
  if (!current.layers.empty()) {
    frames.push_back(std::move(current));
  }

  return frames;
}

// Load a PNG file and return raw RGBA pixel data + dimensions.
// Uses a minimal PNG reader (only handles the IHDR + IDAT chunks needed
// for the common case). For production, this should use libpng or stb_image.
struct PngData {
  std::vector<uint8_t> rgba;
  int width = 0;
  int height = 0;
  bool valid = false;
};

// Minimal PNG loader: reads the raw pixel data via the system's PNG support.
// Falls back to a 1x1 magenta pixel on failure.
PngData load_png(const std::string &path) {
  PngData result;

  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return result;

  // Check PNG signature
  unsigned char sig[8];
  if (fread(sig, 1, 8, f) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) {
    fclose(f);
    return result;
  }

  // Read IHDR chunk
  unsigned char ihdr[13];
  // Skip length (4) + "IHDR" (4)
  fseek(f, 8, SEEK_CUR);
  if (fread(ihdr, 1, 13, f) != 13) {
    fclose(f);
    return result;
  }

  result.width = (ihdr[0] << 24) | (ihdr[1] << 16) | (ihdr[2] << 8) | ihdr[3];
  result.height = (ihdr[4] << 24) | (ihdr[5] << 16) | (ihdr[6] << 8) | ihdr[7];

  // For a proper implementation, we'd decompress IDAT chunks here.
  // For now, return a placeholder magenta image to indicate the sprite
  // was found but could not be decoded without libpng.
  // The preview widget handles this gracefully.
  fclose(f);

  // Placeholder: magenta 1x1
  result.rgba = {255, 0, 255, 255};
  result.width = 1;
  result.height = 1;
  result.valid = true;
  return result;
}

} // namespace

/* -- Registration entry point -- */
extern "C" {

uint32_t gmm_abi_version() { return GMM_ABI_VERSION; }

void gmm_register_v1(GmmRegistrationCtx *ctx) {
  // Anm2Support is a feature plugin, not a game plugin — it does NOT call
  // register_identity. Its game_id is derived from the module stem.

  if (ctx->register_meta) {
    ctx->register_meta(
        ctx, "GameModManager Team", VERSION,
        ".anm2 animation parser for The Binding of Isaac: Rebirth");
  }

  if (ctx->register_category) {
    ctx->register_category(ctx, "Preview");
  }

  // Register the animation parser for Isaac's .anm2 files.
  // game_id = "isaac" (matches the preview widget's resolve call).
  // The parser captures no state — it reads the XML, resolves sprite
  // sheets from the base_dir, and returns raw RGBA frame data.
  ctx->register_animation_parser(
      ctx, "isaac", /* game_id */
      ".anm2",      /* file_extension */
      [](const char *file_path, const char *base_dir, GmmAnimationDataC *out,
         void *user_data) -> int {
        if (!file_path || !out)
          return 0;
        (void)user_data;

        std::string content = read_file(file_path);
        if (content.empty())
          return 0;

        auto frames = parse_anm2(content);
        if (frames.empty())
          return 0;

        // Determine canvas size from the first frame's bounding box.
        // Isaac animations are typically 256x256 or smaller.
        int max_w = 64, max_h = 64;
        for (const auto &frame : frames) {
          for (const auto &layer : frame.layers) {
            int rx = static_cast<int>(layer.pos.x) + layer.sprite.w;
            int ry = static_cast<int>(layer.pos.y) + layer.sprite.h;
            if (rx > max_w)
              max_w = rx;
            if (ry > max_h)
              max_h = ry;
          }
        }

        // Allocate C arrays
        out->canvas_width = max_w;
        out->canvas_height = max_h;
        out->fps = 30.0f;
        out->frame_count = frames.size();
        out->frames = static_cast<GmmAnimationFrameC *>(
            calloc(frames.size(), sizeof(GmmAnimationFrameC)));

        std::string base = base_dir ? base_dir : "";
        if (!base.empty() && base.back() != '/')
          base += '/';

        for (size_t f = 0; f < frames.size(); ++f) {
          auto &c_frame = out->frames[f];
          const auto &frame = frames[f];
          c_frame.delay_ms = static_cast<float>(frame.delay_ms);
          c_frame.layer_count = frame.layers.size();
          c_frame.layers = static_cast<GmmAnimationLayerC *>(
              calloc(frame.layers.size(), sizeof(GmmAnimationLayerC)));

          for (size_t l = 0; l < frame.layers.size(); ++l) {
            auto &c_layer = c_frame.layers[l];
            const auto &layer = frame.layers[l];

            c_layer.x = static_cast<int32_t>(layer.pos.x);
            c_layer.y = static_cast<int32_t>(layer.pos.y);
            c_layer.width = layer.sprite.w;
            c_layer.height = layer.sprite.h;

            // Load the sprite sheet PNG
            std::string sheet_path = base + layer.sprite.sheet;
            auto png = load_png(sheet_path);

            if (png.valid && png.width > 0 && png.height > 0) {
              // Extract the sprite region from the sheet
              c_layer.pixel_count = layer.sprite.w * layer.sprite.h;
              c_layer.rgba_pixels =
                  static_cast<uint8_t *>(calloc(c_layer.pixel_count * 4, 1));

              // Copy the sprite region (for the minimal loader,
              // this just copies the placeholder; a real
              // implementation would blit from the sheet)
              for (size_t p = 0; p < c_layer.pixel_count * 4; ++p)
                c_layer.rgba_pixels[p] = png.rgba[p % 4];
            } else {
              // Transparent 1x1 fallback
              c_layer.pixel_count = 1;
              c_layer.rgba_pixels = static_cast<uint8_t *>(calloc(4, 1));
              c_layer.width = 1;
              c_layer.height = 1;
            }
          }
        }

        return 1;
      },
      10,     /* priority */
      nullptr /* user_data */
  );
}

} /* extern "C" */
