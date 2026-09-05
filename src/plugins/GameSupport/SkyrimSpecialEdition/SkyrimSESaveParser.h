/**
 * Skyrim save parser - C ABI adapters.
 *
 * Three free functions with C linkage
 * matching GmmSaveParserFnV2. The
 * implementations live in SkyrimSESaveParser.cpp;
 * this header is the
 * include the game plugin's gmm_register_v2 uses.
 */
#pragma once

#include "gmm_abi_v2.h"

#ifdef __cplusplus
extern "C"
{
#endif

  int skyrim_save_parser(const char* path, const char* game_id, GmmSaveDataV2* out,
                         void* user_data);

  int skyrimse_save_parser(const char* path, const char* game_id, GmmSaveDataV2* out,
                           void* user_data);

  int skyrimvr_save_parser(const char* path, const char* game_id, GmmSaveDataV2* out,
                          void* user_data);

  GmmSaveOverlayV2* skyrimse_save_overlay(const GmmSaveDataV2* save,
                                          void* user_data);

#ifdef __cplusplus
}
#endif
