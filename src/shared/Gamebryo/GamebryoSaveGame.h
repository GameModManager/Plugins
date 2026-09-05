/**
 * GamebryoSaveGame - shared base class for Bethesda Gamebryo-based save files.
 *

 * * Models MO2's GamebryoSaveGame (REFERENCES/modorganizer-game_bethesda/src/
 *
 * gamebryo/gamebryosavegame.h) as a packet-private helper that the per-game
 *
 * subclasses (SkyrimSaveGame, SkyrimSESaveGame, future FO4) override.
 *
 * Lifetime:
 * the class is constructed by an ABI adapter, parses a single save
 * in its
 * constructor (matches MO2's "construct = parse" contract), and the
 * resulting data
 * is read out via the SaveInfo struct. Keeping the parser
 * state in a member avoids a
 * free-function explosion: a future Bethesda
 * game (FO4, Oblivion, Starfield) only
 * has to override fetchDataFields(),
 * not re-implement the header reader.
 *
 * The
 * class is intentionally NOT a public ABI surface; the only thing that
 * crosses the
 * ABI is GmmSaveDataV2, filled by the per-game adapter.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "GamebryoSaveReader.h"

namespace gmm::gamebryo
{

// One parsed Gamebryo save. Same shape as the old engine::SaveGame
// (header + data region) but lives in the packet so the per-game
// subclasses can write into it without the engine dependency.
struct SaveInfo
{
  std::filesystem::path file_path;
  std::string game_id;  // "skyrim" | "skyrimse" | "skyrimvr" | "fallout4" ...

  // Header fields (MO2 ISaveGame + GamebryoSaveGame simple getters).
  std::int64_t creation_time = 0;  // epoch seconds (game-local wall time)
  std::string pc_name;
  std::uint16_t pc_level = 0;
  std::string pc_location;
  std::uint32_t save_number = 0;

  // Data-region fields (decompressed on demand in MO2; always here).
  std::vector<std::string> plugins;
  std::vector<std::string> light_plugins;
  // medium_plugins is reserved for FO4-style games; left empty by Skyrim.
  std::vector<std::string> medium_plugins;

  // Raw RGB (LE) or RGBA (SE) pixels; packet does not decode the image,
  // the UI does.
  std::vector<std::uint8_t> screenshot;
  int screenshot_width  = 0;
  int screenshot_height = 0;
};

class GamebryoSaveGame
{
public:
  // Opens `path` and verifies the magic. The per-game subclass passes the
  // expected magic ("TESV_SAVEGAME" for Skyrim-family). The parser state
  // lives in the subclass because LE vs SE diverge on the data region.
  GamebryoSaveGame(const std::filesystem::path& path, std::string game_id,
                   const std::string& expected_magic);
  virtual ~GamebryoSaveGame() = default;

  // Disable copy: holds a reader cursor.
  GamebryoSaveGame(const GamebryoSaveGame&)            = delete;
  GamebryoSaveGame& operator=(const GamebryoSaveGame&) = delete;

  // Reads the shared LE/SE header (everything up to and including the
  // FILETIME). Returns the save header version so subclasses can branch on
  // it (e.g. SE version==12 -> RGBA screenshot + u16 compression type).
  std::uint32_t fetch_information_fields();

  // Pure virtual: per-game data region. Subclasses must read the
  // screenshot + (compressed) plugin list, populating data_.
  virtual void fetch_data_fields() = 0;

  // Drives fetch_information_fields + fetch_data_fields. Throws
  // SaveParseError on malformed input. The returned info is a snapshot
  // copy so the caller does not need to keep the instance alive.
  SaveInfo parse();

  // The reader (subclasses need it; the adapter does not).
  [[nodiscard]] GamebryoSaveReader& reader() { return reader_; }
  [[nodiscard]] const SaveInfo& data() const { return data_; }
  [[nodiscard]] SaveInfo& data() { return data_; }

  // Override the script-extender extension (default "skse"). FO4 would
  // override to return "f4se" without duplicating the rest.
  [[nodiscard]] virtual std::string script_extender_extension() const { return "skse"; }

  // True when the script-extender co-save exists next to this save
  // (MO2 GamebryoSaveGame::hasScriptExtenderFile).
  [[nodiscard]] bool has_script_extender_file() const;

protected:
  // Reads the version from fetch_information_fields so subclasses can
  // branch on it before they read width/height.
  std::uint32_t header_version_ = 0;

private:
  GamebryoSaveReader reader_;
  std::string game_id_;
  SaveInfo data_;
};

// Free helpers shared by every subclass (header + plugin list).

// Reads `count` u16-length strings (MO2 readPluginData). Used by the
// decompressed plugin-list and the SE light-plugins list.
[[nodiscard]] std::vector<std::string> read_plugin_list(GamebryoSaveReader& r,
                                                        std::size_t count);

// FILETIME -> epoch seconds, treating the 100ns-since-1601 value as UTC.
// The save's embedded FILETIME is the game's local wall clock at save
// time (the field is never timezone-adjusted by the engine), so this
// conversion deliberately does no timezone math on top.
[[nodiscard]] std::int64_t filetime_to_epoch(std::uint64_t filetime_100ns);

}  // namespace gmm::gamebryo
