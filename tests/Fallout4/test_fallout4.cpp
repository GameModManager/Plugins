/**
 * Fallout 4 game plugin guard tests.
 *
 * Each case runs standalone (argv[1]) against the BUILT plugin .so
 * (dlopen + dlsym) and a recording stub of the registration context - no
 * Core. The stub captures what the plugin registered so the tests assert on
 * the registration OUTPUT, not on the code path that produced it.
 *
 * Cases:
 *   registration        - identity (game_id/display_name/steam_appid/
 *                         nexus_domain), the knowledge hooks Core reads
 *                         (native plugins, Creation Club file, paths,
 *                         save extensions, icon URL), order encoding +
 *                         save parser/overlay registered, feature bits.
 *   order_encoding      - the registered writer produces plugins.txt-style
 *                         output (one name per line).
 *   save_parse          - the registered parser parses a synthetic .fos
 *                         (FO4_SAVEGAME magic, RGBA screenshot, uncompressed
 *                         plugin lists, light plugins from version 68).
 *   save_parse_rejects  - wrong magic / truncated file -> parser returns 0.
 */

#include "gmm_abi_v2.h"

#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef GMM_FALLOUT4_PLUGIN_PATH
#define GMM_FALLOUT4_PLUGIN_PATH "Fallout4.so"
#endif

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const char *expr, int line, const std::string &detail = "") {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::fprintf(stderr, "FAIL line %d: %s %s\n", line, expr, detail.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void check_eq(const std::string &got, const char *want, const char *expr, int line) {
  ++g_checks;
  if (got != (want ? want : "")) {
    ++g_failures;
    std::fprintf(stderr, "FAIL line %d: %s (got \"%s\", want \"%s\")\n", line, expr,
                 got.c_str(), want ? want : "");
  }
}
#define CHECK_EQ(got, want) check_eq((got), (want), #got " == " #want, __LINE__)

/* -- Recording stub of GmmRegistrationCtxV2 -- */

struct Recording {
  bool game_registered = false;
  std::string game_id, display_name, nexus_domain, exe_windows;
  uint32_t steam_appid = 0;
  std::vector<std::pair<std::string, std::string>> hooks;  // tag, data
  bool order_encoding_registered         = false;
  GmmOrderEncodingFnV2 order_encoding_fn = nullptr;
  GmmSaveParserFnV2 save_parser_fn       = nullptr;
  std::string save_parser_game_id;
  GmmSaveOverlayFnV2 save_overlay_fn = nullptr;
  std::vector<std::pair<std::string, std::string>> tools;  // tool_id, kind
};

Recording g_rec;

void rec_game(GmmRegistrationCtxV2 *, GmmGameInfo info) {
  g_rec.game_registered = true;
  g_rec.game_id         = info.game_id ? info.game_id : "";
  g_rec.display_name    = info.display_name ? info.display_name : "";
  g_rec.steam_appid     = info.steam_appid;
  g_rec.nexus_domain    = info.nexus_domain ? info.nexus_domain : "";
  g_rec.exe_windows     = info.exe_windows ? info.exe_windows : "";
}
void rec_plugin(GmmRegistrationCtxV2 *, GmmPluginInfo) {}
void rec_category(GmmRegistrationCtxV2 *, const char *) {}
void rec_tabs(GmmRegistrationCtxV2 *, const GmmTabInfo *, size_t) {}
void rec_hook(GmmRegistrationCtxV2 *, const char *tag, const char *data, GmmHookFnV2,
              int, void *) {
  g_rec.hooks.emplace_back(tag ? tag : "", data ? data : "");
}
void rec_order_encoding(GmmRegistrationCtxV2 *, GmmOrderEncodingFnV2 fn, void *) {
  g_rec.order_encoding_registered = true;
  g_rec.order_encoding_fn         = fn;
}
void rec_save_parser(GmmRegistrationCtxV2 *, const char *game_id, GmmSaveParserFnV2 fn,
                     int, void *) {
  g_rec.save_parser_game_id = game_id ? game_id : "";
  g_rec.save_parser_fn      = fn;
}
void rec_save_overlay(GmmRegistrationCtxV2 *, const char *, GmmSaveOverlayFnV2 fn, int,
                      void *) {
  g_rec.save_overlay_fn = fn;
}
void rec_tool(GmmRegistrationCtxV2 *, const char *tool_id, const char *kind,
              GmmToolInvokeFn, void *) {
  g_rec.tools.emplace_back(tool_id ? tool_id : "", kind ? kind : "");
}

GmmRegistrationCtxV2 make_stub_ctx() {
  GmmRegistrationCtxV2 ctx;
  std::memset(&ctx, 0, sizeof(ctx));
  ctx.register_game           = rec_game;
  ctx.register_plugin         = rec_plugin;
  ctx.register_category       = rec_category;
  ctx.register_tabs           = rec_tabs;
  ctx.register_hook           = rec_hook;
  ctx.register_order_encoding = rec_order_encoding;
  ctx.register_save_parser    = rec_save_parser;
  ctx.register_save_overlay   = rec_save_overlay;
  ctx.register_tool           = rec_tool;
  return ctx;
}

const std::string *find_hook(const std::string &tag) {
  for (const auto &h : g_rec.hooks)
    if (h.first == tag)
      return &h.second;
  return nullptr;
}

bool has_hook(const std::string &tag) {
  return find_hook(tag) != nullptr;
}

/* -- Load the built plugin -- */

void *g_so = nullptr;

bool load_plugin() {
  g_so = dlopen(GMM_FALLOUT4_PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
  if (!g_so) {
    std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return false;
  }
  return true;
}

void run_registration() {
  auto reg = reinterpret_cast<void (*)(GmmRegistrationCtxV2 *)>(
      dlsym(g_so, "gmm_register_v2"));
  auto ver   = reinterpret_cast<uint32_t (*)(void)>(dlsym(g_so, "gmm_abi_version"));
  auto feats = reinterpret_cast<uint64_t (*)(void)>(dlsym(g_so, "gmm_abi_features"));
  CHECK(reg != nullptr);
  CHECK(ver != nullptr);
  if (!reg || !ver)
    return;
  CHECK(ver() == 2);

  GmmRegistrationCtxV2 ctx = make_stub_ctx();
  reg(&ctx);

  /* -- Identity -- */
  CHECK(g_rec.game_registered);
  CHECK_EQ(g_rec.game_id, "Fallout4");
  CHECK_EQ(g_rec.display_name, "Fallout 4");
  CHECK(g_rec.steam_appid == 377160);
  CHECK_EQ(g_rec.nexus_domain, "fallout4");
  /* exe_windows is nullptr like SkyrimSE's - the engine detects the binary
   * at runtime (the "executables" hook carries the names). */
  CHECK(g_rec.exe_windows.empty());

  /* -- Knowledge hooks Core reads -- */
  const std::string *appid = find_hook("steam_appid");
  CHECK(appid && *appid == "377160");

  const std::string *natives = find_hook("game_native_plugins");
  CHECK(natives != nullptr);
  if (natives) {
    /* Base game + every DLC esm, comma-separated. */
    CHECK(natives->find("Fallout4.esm") != std::string::npos);
    CHECK(natives->find("DLCRobot.esm") != std::string::npos);
    CHECK(natives->find("DLCworkshop01.esm") != std::string::npos);
    CHECK(natives->find("DLCCoast.esm") != std::string::npos);
    CHECK(natives->find("DLCworkshop02.esm") != std::string::npos);
    CHECK(natives->find("DLCworkshop03.esm") != std::string::npos);
    CHECK(natives->find("DLCNukaWorld.esm") != std::string::npos);
    CHECK(natives->find("DLCUltraHighResolution.esm") != std::string::npos);
  }

  const std::string *ccc = find_hook("creation_club_file");
  CHECK(ccc && *ccc == "Fallout4.ccc");

  const std::string *mygames = find_hook("mygames_folder");
  CHECK(mygames && *mygames == "Fallout4");
  const std::string *localappdata = find_hook("localappdata_folder");
  CHECK(localappdata && *localappdata == "Fallout4");
  const std::string *mods_subpath = find_hook("mods_subpath");
  CHECK(mods_subpath && *mods_subpath == "Data");
  const std::string *save_ext = find_hook("save_extensions");
  CHECK(save_ext && *save_ext == "fos");

  const std::string *dirs = find_hook("mod_valid_dirs");
  CHECK(dirs != nullptr);
  if (dirs) {
    CHECK(dirs->find("textures") != std::string::npos);
    CHECK(dirs->find("meshes") != std::string::npos);
    CHECK(dirs->find("f4se") != std::string::npos);
  }
  const std::string *exts = find_hook("mod_valid_exts");
  CHECK(exts != nullptr);
  if (exts) {
    CHECK(exts->find("esp") != std::string::npos);
    CHECK(exts->find("esl") != std::string::npos);
    CHECK(exts->find("ba2") != std::string::npos);
  }

  const std::string *icon = find_hook("game_icon_url");
  CHECK(icon != nullptr);
  if (icon) {
    CHECK(icon->rfind("https://cdn.cloudflare.steamstatic.com/", 0) == 0);
    CHECK(icon->find("/apps/377160/") != std::string::npos);
  }

  const std::string *loot = find_hook("loot_game_id");
  CHECK(loot && *loot == "fallout4");

  /* -- Order encoding + save parser/overlay registered -- */
  CHECK(g_rec.order_encoding_registered);
  CHECK(g_rec.order_encoding_fn != nullptr);
  CHECK_EQ(g_rec.save_parser_game_id, "Fallout4");
  CHECK(g_rec.save_parser_fn != nullptr);
  CHECK(g_rec.save_overlay_fn != nullptr);

  /* Tool: LOOT, advisory (same shape as SkyrimSE). */
  CHECK(g_rec.tools.size() == 1);
  if (g_rec.tools.size() == 1) {
    CHECK_EQ(g_rec.tools[0].first, "loot");
    CHECK_EQ(g_rec.tools[0].second, "advisory");
  }

  /* Feature bits: screenshot + all_files + overlay, no variants. */
  if (feats) {
    const uint64_t f = feats();
    CHECK((f & (1ull << 0)) != 0);  // SAVE_SCREENSHOT
    CHECK((f & (1ull << 1)) != 0);  // SAVE_ALL_FILES
    CHECK((f & (1ull << 3)) != 0);  // SAVE_OVERLAY
    CHECK((f & (1ull << 4)) == 0);  // GAME_VARIANTS not registered
  }
}

void run_order_encoding() {
  if (!g_rec.order_encoding_fn)
    return;
  std::filesystem::path out =
      std::filesystem::temp_directory_path() / "gmm_fo4_test_plugins.txt";
  const char *mods[] = {"Fallout4.esm", "MyMod.esp"};
  CHECK(g_rec.order_encoding_fn(mods, 2, out.string().c_str(), nullptr) == 1);
  std::ifstream f(out);
  std::string line1, line2, line3;
  std::getline(f, line1);
  std::getline(f, line2);
  std::getline(f, line3);
  CHECK_EQ(line1, "Fallout4.esm");
  CHECK_EQ(line2, "MyMod.esp");
  CHECK(line3.empty());  // exactly two lines
  std::error_code ec;
  std::filesystem::remove(out, ec);
}

/* -- Synthetic .fos builder -- */

void put_u32(std::string &s, uint32_t v) {
  s.append(std::string(reinterpret_cast<const char *>(&v), 4));
}
void put_u16(std::string &s, uint16_t v) {
  s.append(std::string(reinterpret_cast<const char *>(&v), 2));
}
void put_wstring(std::string &s, const std::string &v) {
  put_u16(s, static_cast<uint16_t>(v.size()));
  s.append(v);
}

std::string make_fo4_save(uint8_t save_game_version) {
  std::string s;
  s.append("FO4_SAVEGAME");
  put_u32(s, 0);   // header size (ignored)
  put_u32(s, 1);   // header version
  put_u32(s, 42);  // save number
  put_wstring(s, "Sole Survivor");
  put_u32(s, 27);  // level
  put_wstring(s, "Sanctuary");
  put_wstring(s, "01.02.03");  // playtime
  put_wstring(s, "HumanRace");
  put_u16(s, 0);  // gender
  put_u32(s, 0);  // exp
  put_u32(s, 0);  // exp required
  put_u32(s, 0);  // FILETIME lo
  put_u32(s, 0);  // FILETIME hi

  // Data region: RGBA screenshot 2x2.
  put_u32(s, 2);
  put_u32(s, 2);
  for (int i = 0; i < 4; ++i) {
    s.append(std::string("\x10\x20\x30\xFF", 4));
  }
  s.push_back(static_cast<char>(save_game_version));
  put_wstring(s, "1.10.163");
  put_u32(s, 0);                      // plugin info size
  s.push_back(static_cast<char>(2));  // plugin count
  put_wstring(s, "Fallout4.esm");
  put_wstring(s, "MyMod.esp");
  if (save_game_version >= 68) {
    put_u16(s, 1);  // light plugin count
    put_wstring(s, "ccTest.esl");
  }
  return s;
}

void run_save_parse() {
  if (!g_rec.save_parser_fn)
    return;
  std::filesystem::path path =
      std::filesystem::temp_directory_path() / "gmm_fo4_test_save.fos";
  {
    std::ofstream f(path, std::ios::binary);
    f.write(make_fo4_save(68).data(),
            static_cast<std::streamsize>(make_fo4_save(68).size()));
  }
  GmmSaveDataV2 save;
  std::memset(&save, 0xAA, sizeof(save));  // poison: parser must fill it all
  CHECK(g_rec.save_parser_fn(path.string().c_str(), "Fallout4", &save, nullptr) == 1);
  CHECK_EQ(std::string(save.game_id ? save.game_id : ""), "Fallout4");
  CHECK_EQ(std::string(save.pc_name ? save.pc_name : ""), "Sole Survivor");
  CHECK(save.pc_level == 27);
  CHECK_EQ(std::string(save.pc_location ? save.pc_location : ""), "Sanctuary");
  CHECK(save.save_number == 42);
  CHECK(save.plugin_count == 2);
  if (save.plugin_count == 2) {
    CHECK_EQ(std::string(save.plugins[0] ? save.plugins[0] : ""), "Fallout4.esm");
    CHECK_EQ(std::string(save.plugins[1] ? save.plugins[1] : ""), "MyMod.esp");
  }
  CHECK(save.light_plugin_count == 1);
  if (save.light_plugin_count == 1) {
    CHECK_EQ(std::string(save.light_plugins[0] ? save.light_plugins[0] : ""),
             "ccTest.esl");
  }
  CHECK(save.screenshot_width == 2);
  CHECK(save.screenshot_height == 2);
  CHECK(save.screenshot_size == 16);
  if (save.screenshot_rgba && save.screenshot_size == 16) {
    CHECK(save.screenshot_rgba[0] == 0x10);
    CHECK(save.screenshot_rgba[3] == 0xFF);
  }
  /* all_files: the save itself (no .f4se co-save sibling exists). */
  CHECK(save.all_files_count == 1);

  /* Wrong magic -> 0, not a crash. */
  std::filesystem::path bad =
      std::filesystem::temp_directory_path() / "gmm_fo4_test_bad.fos";
  {
    std::ofstream f(bad, std::ios::binary);
    f << "TESV_SAVEGAME garbage";
  }
  GmmSaveDataV2 rejected;
  std::memset(&rejected, 0, sizeof(rejected));
  CHECK(g_rec.save_parser_fn(bad.string().c_str(), "Fallout4", &rejected, nullptr) ==
        0);

  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(bad, ec);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: test_fallout4 <case>\n");
    return 2;
  }
  const std::string casename = argv[1];

  if (!load_plugin()) {
    std::fprintf(stderr, "%s: cannot load " GMM_FALLOUT4_PLUGIN_PATH "\n",
                 casename.c_str());
    return 1;
  }

  /* Every case starts from a fresh registration so cases stay independent. */
  if (casename == "registration")
    run_registration();
  else if (casename == "order_encoding") {
    run_registration();
    run_order_encoding();
  } else if (casename == "save_parse") {
    run_registration();
    run_save_parse();
  } else {
    std::fprintf(stderr, "unknown case: %s\n", casename.c_str());
    dlclose(g_so);
    return 2;
  }

  dlclose(g_so);

  if (g_failures > 0) {
    std::fprintf(stderr, "%s: %d failure(s)\n", casename.c_str(), g_failures);
    return 1;
  }
  return 0;
}
