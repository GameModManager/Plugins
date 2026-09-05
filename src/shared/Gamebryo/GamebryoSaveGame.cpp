#include "GamebryoSaveGame.h"

#include <filesystem>

namespace gmm::gamebryo
{

namespace
{

  // 100ns ticks between 1601-01-01 (FILETIME epoch) and 1970-01-01 (Unix epoch).
  constexpr std::uint64_t kFiletimeToUnixEpoch = 116444736000000000ULL;
  constexpr std::uint64_t kTicksPerSecond      = 10'000'000ULL;

}  // namespace

GamebryoSaveGame::GamebryoSaveGame(const std::filesystem::path& path,
                                   std::string game_id,
                                   const std::string& expected_magic)
    : reader_(path, expected_magic), game_id_(std::move(game_id))
{
  data_.file_path = path;
  data_.game_id   = game_id_;
}

std::uint32_t GamebryoSaveGame::fetch_information_fields()
{
  auto& r = reader_;
  r.skip(4);  // header size (does NOT bound the strings; MO2 ignores it)
  std::uint32_t version = r.u32();
  header_version_       = version;
  data_.save_number     = r.u32();
  data_.pc_name         = r.wstring();
  std::uint32_t level   = r.u32();
  data_.pc_level        = static_cast<std::uint16_t>(level);
  data_.pc_location     = r.wstring();
  r.wstring();  // time of day
  r.wstring();  // race
  r.skip(2);    // player gender (0 = male)
  r.skip(8);    // experience gathered, experience required (2 floats)
  data_.creation_time = filetime_to_epoch(r.u64());
  return version;
}

SaveInfo GamebryoSaveGame::parse()
{
  fetch_information_fields();
  fetch_data_fields();
  // game_id may have been overwritten by the subclass (e.g. "skyrimse"
  // when the same SE parser is used for "skyrimvr"); restore the
  // caller's tag so the registry's parser lookup stays stable.
  data_.game_id = game_id_;
  return data_;
}

bool GamebryoSaveGame::has_script_extender_file() const
{
  if (!data_.file_path.has_extension()) {
    return false;
  }
  auto co = data_.file_path;
  co.replace_extension("." + script_extender_extension());
  std::error_code ec;
  return std::filesystem::exists(co, ec);
}

std::vector<std::string> read_plugin_list(GamebryoSaveReader& r, std::size_t count)
{
  std::vector<std::string> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(r.wstring());
  }
  return out;
}

std::int64_t filetime_to_epoch(std::uint64_t filetime_100ns)
{
  if (filetime_100ns < kFiletimeToUnixEpoch) {
    return 0;
  }
  return static_cast<std::int64_t>((filetime_100ns - kFiletimeToUnixEpoch) /
                                   kTicksPerSecond);
}

}  // namespace gmm::gamebryo
