#include "SkyrimSESaveGame.h"

namespace gmm::gamebryo
{

SkyrimSESaveGame::SkyrimSESaveGame(const std::filesystem::path& path,
                                   std::string game_id)
    : GamebryoSaveGame(path, std::move(game_id), "TESV_SAVEGAME")
{}

void SkyrimSESaveGame::fetch_data_fields()
{
  auto& r = reader();

  std::uint32_t w = r.u32();
  std::uint32_t h = r.u32();
  // SE version 12 adds a u16 compression type and an alpha channel
  // (MO2 SkyrimSESaveGame::fetchDataFields).
  bool alpha                = false;
  std::uint16_t compression = 0;
  if (header_version_ == 12) {
    compression = r.u16();
    alpha       = true;
  }
  std::string screenshot =
      r.read_bytes(static_cast<std::size_t>(w) * h * (alpha ? 4u : 3u));
  data().screenshot.assign(screenshot.begin(), screenshot.end());
  data().screenshot_width  = static_cast<int>(w);
  data().screenshot_height = static_cast<int>(h);

  r.begin_compressed(compression);
  std::uint8_t save_game_version = r.u8();
  r.u8();     // plugin info size (u8 in SE)
  r.u16();    // "other" (unknown)
  r.skip(1);  // pad byte before the plugin count
  data().plugins = read_plugin_list(r, r.u8());
  if (save_game_version >= 78) {
    data().light_plugins = read_plugin_list(r, r.u16());
  }
}

}  // namespace gmm::gamebryo
