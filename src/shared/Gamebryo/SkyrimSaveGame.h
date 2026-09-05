/**
 * SkyrimSaveGame - Skyrim Legendary Edition save (game_id="skyrim").
 *
 *
 * Data-region shape (MO2 SkyrimSaveGame::fetchDataFields):
 *   u32 width, u32 height,
 * RGB screenshot (w*h*3 bytes),
 *   begin_compressed(0), u8 form_version, u32
 * plugin_info_size,
 *   u8 plugin_count, then `plugin_count` plugin names.
 *
 * No
 * compression-type field, no light plugins, no medium plugins. The
 * compressed region
 * is always raw (begin_compressed with type 0).
 */
#pragma once

#include "GamebryoSaveGame.h"

namespace gmm::gamebryo
{

class SkyrimSaveGame : public GamebryoSaveGame
{
public:
  SkyrimSaveGame(const std::filesystem::path& path, std::string game_id);

  void fetch_data_fields() override;
};

}  // namespace gmm::gamebryo
