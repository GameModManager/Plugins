#include "Fallout4SaveGame.h"

namespace gmm::gamebryo {

Fallout4SaveGame::Fallout4SaveGame(const std::filesystem::path &path,
                                   std::string game_id)
    : GamebryoSaveGame(path, std::move(game_id), "FO4_SAVEGAME") {}

void Fallout4SaveGame::fetch_data_fields() {
  auto &r = reader();

  // Screenshot: u32 width, u32 height, then w*h*4 RGBA bytes (MO2
  // Fallout4SaveGame::fetchDataFields -> readImage(384, true); the 384 is a
  // display scale, not a layout fact, so the packet stores the full image).
  std::uint32_t w        = r.u32();
  std::uint32_t h        = r.u32();
  std::string screenshot = r.read_bytes(static_cast<std::size_t>(w) * h * 4u);
  data().screenshot.assign(screenshot.begin(), screenshot.end());
  data().screenshot_width  = static_cast<int>(w);
  data().screenshot_height = static_cast<int>(h);

  std::uint8_t save_game_version = r.u8();
  r.wstring();  // game version
  r.skip(4);    // plugin info size

  // Plugin lists are uncompressed for FO4 (MO2 FileWrapper compression type
  // stays 0): u8 main count, then u16 light count from save-game
  // version 68 (next-gen update).
  data().plugins = read_plugin_list(r, r.u8());
  if (save_game_version >= 68) {
    data().light_plugins = read_plugin_list(r, r.u16());
  }
}

}  // namespace gmm::gamebryo
