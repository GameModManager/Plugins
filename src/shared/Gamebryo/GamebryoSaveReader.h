/**
 * GamebryoSaveReader - vendored copy of Core's engine::SaveReader.
 *
 * The packet
 * is intentionally Core-independent: a future Bethesda game plugin
 * (FO4, Oblivion,
 * Starfield) links GMM_Gamebryo without pulling in any
 * engine headers. This file
 * duplicates Core's save_reader.h/.cpp byte-for-byte
 * except for the namespace and
 * class name. If the two copies ever drift,
 * re-vendor: see
 * Projects/Plugins/src/shared/Gamebryo/AGENTS.md (TODO).
 *
 * The reading primitives
 * and the zlib/LZ4 decompression logic are
 * deliberately unchanged from MO2
 * (FileWrapper + openCompressedData) so the
 * SaveGame metadata this packet produces
 * is bit-identical to what Core used
 * to produce.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace gmm::gamebryo
{

// Thrown on any malformed/truncated input. Mirrors MO2's std::runtime_error
// contract so the engine-side scanner's `catch` keeps working unchanged.
class SaveParseError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class GamebryoSaveReader
{
public:
  // Loads `path`, verifies the first `expected_magic` bytes, positions the
  // cursor just past the magic. Throws SaveParseError on open failure or
  // magic mismatch.
  GamebryoSaveReader(const std::filesystem::path& path,
                     const std::string& expected_magic);

  // --- raw little-endian primitives; all throw SaveParseError on EOF ---
  std::uint8_t u8();
  std::uint16_t u16();
  std::uint32_t u32();
  std::uint64_t u64();
  float f32();
  void skip(std::size_t count);
  std::string read_bytes(std::size_t count);

  // u16 byte-length + payload, decoded as UTF-8. MO2 FileWrapper
  // read<QString> (WSTRING, UTF8). The header strings are single-byte in
  // real saves ("Vanilla Vanny" = 13 ASCII bytes), so a plain UTF-8 decode
  // is correct; MO2's LOCAL8BIT variant for the LE location field decodes
  // to UTF-8 on any modern Linux locale anyway.
  std::string wstring();

  // Reads the compression header for the data region and switches all
  // subsequent reads to the decompressed buffer. `type` is the u16 read by
  // the SE parser after width/height. Throws SaveParseError on unknown
  // compression or a failed decompress.
  void begin_compressed(std::uint16_t type);

  [[nodiscard]] std::size_t position() const { return pos_; }
  [[nodiscard]] std::size_t size() const { return buf_.size(); }

private:
  static std::vector<std::uint8_t>
  inflate_chunks(std::uint64_t start, std::uint64_t total_uncompressed,
                 const std::vector<std::uint8_t>& file);
  static std::vector<std::uint8_t> lz4_decompress(const std::string& compressed,
                                                  std::uint32_t uncompressed_size);

  std::vector<std::uint8_t> buf_;   // file content, or decompressed region
  std::vector<std::uint8_t> file_;  // full raw file (kept for chunked reads)
  std::size_t pos_ = 0;
};

}  // namespace gmm::gamebryo
