/**
 * SkyrimSESaveGame - Skyrim Special Edition save (game_id="skyrimse" or
 *
 * "skyrimvr"). Same magic and same header shape as LE; diverges on the
 * data region.

 * *
 * Data-region shape (MO2 SkyrimSESaveGame::fetchDataFields):
 *   u32 width, u32
 * height,
 *   [if header version == 12: u16 compression_type, RGBA screenshot]
 *
 * [else:                          RGB screenshot]
 *
 * begin_compressed(compression_type),
 *   u8 save_game_version,
 *   u8
 * plugin_info_size,
 *   u16 unknown ("other"),
 *   u8 pad,
 *   u8 plugin_count, then
 * `plugin_count` plugin names,
 *   [if save_game_version >= 78: u16
 * light_plugin_count, then names].
 *
 * Used for both "skyrimse" and "skyrimvr" - the
 * SE plugin can also parse
 * VR saves (MO2 does the same).
 */
#pragma once

#include "GamebryoSaveGame.h"

namespace gmm::gamebryo
{

class SkyrimSESaveGame : public GamebryoSaveGame
{
public:
  SkyrimSESaveGame(const std::filesystem::path& path, std::string game_id);

  void fetch_data_fields() override;
};

}  // namespace gmm::gamebryo
