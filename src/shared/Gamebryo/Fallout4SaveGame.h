/**
 * Fallout4SaveGame - FO4 .fos save parsing on the shared Gamebryo packet.
 *
 * Models MO2's Fallout4SaveGame (REFERENCES/modorganizer-game_bethesda/src/
 * games/fallout4/fallout4savegame.cpp). The header layout is identical to the
 * Skyrim-family one the base class already reads (magic aside), so only the
 * data region is overridden:
 *   - RGBA screenshot (alpha=true in MO2's readImage call),
 *   - u8 save-game version + game-version string + plugin-info size,
 *   - UNCOMPRESSED plugin lists (FO4 never sets a compression type):
 *     u8 count + wstrings, then u16 light-plugin count + wstrings when the
 *     save-game version is >= 68.
 */
#pragma once

#include "GamebryoSaveGame.h"

namespace gmm::gamebryo {

class Fallout4SaveGame : public GamebryoSaveGame {
public:
  Fallout4SaveGame(const std::filesystem::path &path, std::string game_id);

  void fetch_data_fields() override;

  // FO4's script extender is F4SE; the co-save sits next to the .fos as
  // ".f4se" (MO2 GameFallout4::savegameSEExtension).
  [[nodiscard]] std::string script_extender_extension() const override {
    return "f4se";
  }
};

}  // namespace gmm::gamebryo
