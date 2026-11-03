/**
 * GamebryoSaveAbi - shared GmmSaveDataV2 / overlay builders for the per-game
 * save-parser adapters.
 *
 * Both Bethesda game plugins (SkyrimSpecialEdition, Fallout4) turn a parsed
 * gmm::gamebryo::SaveInfo into the ABI structs the engine consumes. The
 * translation is identical except for the script-extender co-save extension
 * (.skse vs .f4se), so it lives here once instead of being copied per game.
 *
 * Packet-internal: crosses the ABI only through the malloc'd structs the
 * engine already owns (see cb_v2_register_save_parser in Core's
 * plugin_loader.cpp for the free-side contract).
 */
#pragma once

#include "GamebryoSaveGame.h"

#include "gmm_abi_v2.h"

namespace gmm::gamebryo {

// Translate a parsed SaveInfo into a freshly malloc'd GmmSaveDataV2.
// co_save_ext is the script-extender co-save extension WITHOUT the leading
// dot ("skse", "f4se"); a sibling file with that extension is added to
// all_files when it exists. Caller takes ownership of every char* and the
// top-level struct (the engine frees them after copying the strings into a
// SaveGame). Returns nullptr on allocation failure.
[[nodiscard]] GmmSaveDataV2 *save_info_to_abi(const SaveInfo &info,
                                              const char *co_save_ext);

// Generic Gamebryo save overlay: "Save details" + save number / plugin
// counts. Matches GmmSaveOverlayFnV2 so adapters can register it directly.
GmmSaveOverlayV2 *gamebryo_save_overlay(const GmmSaveDataV2 *save, void *user_data);

}  // namespace gmm::gamebryo
