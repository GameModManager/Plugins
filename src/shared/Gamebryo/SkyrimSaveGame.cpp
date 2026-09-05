#include "SkyrimSaveGame.h"

namespace gmm::gamebryo
{

SkyrimSaveGame::SkyrimSaveGame(const std::filesystem::path& path, std::string game_id)
    : GamebryoSaveGame(path, std::move(game_id), "TESV_SAVEGAME")
{}

void SkyrimSaveGame::fetch_data_fields()
{
  auto& r = reader();

  std::uint32_t w = r.u32();
  std::uint32_t h = r.u32();
  // LE: RGB, no alpha, no compression-type field
  // (MO2 SkyrimSaveGame::fetchDataFields).
  std::string pixels = r.read_bytes(static_cast<std::size_t>(w) * h * 3);
  data().screenshot.assign(pixels.begin(), pixels.end());
  data().screenshot_width  = static_cast<int>(w);
  data().screenshot_height = static_cast<int>(h);

  r.begin_compressed(0);
  r.skip(1);  // form version
  r.skip(4);  // plugin info size (u32 in LE)
  data().plugins = read_plugin_list(r, r.u8());
  // LE has no light plugins (MO2 never reads them for LE).
}

}  // namespace gmm::gamebryo
