/**
 * BasicGame guard tests.
 *
 * Each case runs standalone (argv[1]) against a recording stub of the
 * registration context - no Core, no dlopen. The stub captures what the
 * plugin registered so the tests assert on the registration OUTPUT, not on
 * the code path that produced it.
 *
 * Cases:
 *   missing_required_refuses   - a required field left out: register_all
 *                                returns false, the error names the field,
 *                                and NOTHING was registered.
 *   missing_required_names_first - nothing set: the error names game_id.
 *   defaults_apply             - untouched fields ride the documented
 *                                defaults (exe_windows, nexus_domain,
 *                                description, localappdata_folder,
 *                                mygames_folder, deploy_prefix,
 *                                case_sensitive, executables).
 *   overrides_win              - explicitly set fields beat the defaults.
 *   extra_hooks_run            - extra(Ctx&) is called last and its hooks
 *                                land.
 */

#include "BasicGame.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

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

/* Value equality: strcmp for C strings (pointer == would compare
 * addresses), == for everything else. */
inline bool values_equal(const char *a, const char *b) {
  return std::strcmp(a ? a : "", b ? b : "") == 0;
}
inline bool values_equal(const std::string &a, const char *b) {
  return a == (b ? b : "");
}
inline bool values_equal(const char *a, const std::string &b) {
  return (a ? a : "") == b;
}
template <class A, class B>
bool values_equal(const A &a, const B &b) {
  return a == b;
}

template <typename A, typename B>
void check_eq(A got, B want, const char *expr, int line) {
  ++g_checks;
  if (!values_equal(got, want)) {
    ++g_failures;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
  }
}
#define CHECK_EQ(got, want) check_eq((got), (want), #got " == " #want, __LINE__)

/* -- Recording stub of GmmRegistrationCtxV2 -- */

struct Recording {
  /* Deep copies: Core's registration callbacks copy every string into
   * std::string synchronously (plugin_loader.cpp), so the stub must too -
   * holding the struct's pointers would dangle after register_all
   * returns. */
  bool game_registered = false;
  std::string game_id, display_name, nexus_domain, exe_windows, exe_linux, exe_macos,
      gog_id, epic_namespace;
  uint32_t steam_appid   = 0;
  bool plugin_registered = false;
  std::string plugin_name, plugin_author, plugin_version, plugin_description;
  std::vector<std::string> categories;
  std::vector<std::pair<std::string, std::string>> tabs;   // capability, data_path
  std::vector<std::pair<std::string, std::string>> hooks;  // tag, data
  bool order_encoding_registered = false;
};

Recording g_rec;

void rec_game(GmmRegistrationCtxV2 *, GmmGameInfo info) {
  g_rec.game_registered = true;
  g_rec.game_id         = info.game_id ? info.game_id : "";
  g_rec.display_name    = info.display_name ? info.display_name : "";
  g_rec.steam_appid     = info.steam_appid;
  g_rec.gog_id          = info.gog_id ? info.gog_id : "";
  g_rec.epic_namespace  = info.epic_namespace ? info.epic_namespace : "";
  g_rec.nexus_domain    = info.nexus_domain ? info.nexus_domain : "";
  g_rec.exe_windows     = info.exe_windows ? info.exe_windows : "";
  g_rec.exe_linux       = info.exe_linux ? info.exe_linux : "";
  g_rec.exe_macos       = info.exe_macos ? info.exe_macos : "";
}
void rec_plugin(GmmRegistrationCtxV2 *, GmmPluginInfo info) {
  g_rec.plugin_registered  = true;
  g_rec.plugin_name        = info.name ? info.name : "";
  g_rec.plugin_author      = info.author ? info.author : "";
  g_rec.plugin_version     = info.version ? info.version : "";
  g_rec.plugin_description = info.description ? info.description : "";
}
void rec_category(GmmRegistrationCtxV2 *, const char *name) {
  g_rec.categories.emplace_back(name ? name : "");
}
void rec_tabs(GmmRegistrationCtxV2 *, const GmmTabInfo *tabs, size_t count) {
  for (size_t i = 0; i < count; i++)
    g_rec.tabs.emplace_back(tabs[i].capability ? tabs[i].capability : "",
                            tabs[i].data_path ? tabs[i].data_path : "");
}
void rec_hook(GmmRegistrationCtxV2 *, const char *tag, const char *data, GmmHookFnV2,
              int, void *) {
  g_rec.hooks.emplace_back(tag ? tag : "", data ? data : "");
}
void rec_order_encoding(GmmRegistrationCtxV2 *, GmmOrderEncodingFnV2, void *) {
  g_rec.order_encoding_registered = true;
}

/* Build a zero-initialised ctx with only the slots BasicGame's chain
 * uses. Unset slots stay null - a call through one would segfault, which
 * is itself a test failure. */
GmmRegistrationCtxV2 make_stub_ctx() {
  GmmRegistrationCtxV2 ctx;
  std::memset(&ctx, 0, sizeof(ctx));
  ctx.register_game           = rec_game;
  ctx.register_plugin         = rec_plugin;
  ctx.register_category       = rec_category;
  ctx.register_tabs           = rec_tabs;
  ctx.register_hook           = rec_hook;
  ctx.register_order_encoding = rec_order_encoding;
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

void reset() {
  g_rec = Recording{};
}

/* -- Test games -- */

/* Missing game_binary (required). */
struct IncompleteGame : gmm::BasicGame {
  IncompleteGame() {
    game_id      = "IncompleteGame";
    display_name = "Incomplete Game";
    mods_subpath = "mods";
  }
};

/* Nothing set at all. */
struct EmptyGame : gmm::BasicGame {};

/* Minimal complete game - everything else rides defaults. */
struct MinimalGame : gmm::BasicGame {
  MinimalGame() {
    game_id      = "MinimalGame";
    display_name = "Minimal Game";
    game_binary  = "minimal.exe";
    mods_subpath = "mods";
  }
};

/* Explicit overrides + an extra() hook. */
struct OverridingGame : gmm::BasicGame {
  OverridingGame() {
    game_id         = "OverridingGame";
    display_name    = "Overriding Game";
    game_binary     = "over.exe";
    mods_subpath    = "Mods";
    steam_appid     = 4242;
    nexus_domain    = "overridinggame";
    save_extensions = "sav";
    description     = "Custom description.";
    case_sensitive  = true;
  }

  void extra(gmm::Ctx &ctx) const override {
    ctx.hook("ignored_files", ".git,Thumbs.db");
  }
};

/* -- Cases -- */

void missing_required_refuses() {
  reset();
  GmmRegistrationCtxV2 ctx = make_stub_ctx();
  IncompleteGame game;
  std::string err;
  bool ok = game.register_all(&ctx, &err);
  CHECK(!ok);
  CHECK(err.find("game_binary") != std::string::npos);
  CHECK(!g_rec.game_registered); /* nothing registered: no half-load */
  CHECK(!g_rec.plugin_registered);
  CHECK(g_rec.hooks.empty());
}

void missing_required_names_first() {
  reset();
  GmmRegistrationCtxV2 ctx = make_stub_ctx();
  EmptyGame game;
  std::string err;
  bool ok = game.register_all(&ctx, &err);
  CHECK(!ok);
  CHECK(err.find("game_id") != std::string::npos);
  CHECK(!g_rec.game_registered);
}

void defaults_apply() {
  reset();
  GmmRegistrationCtxV2 ctx = make_stub_ctx();
  MinimalGame game;
  std::string err;
  CHECK(game.register_all(&ctx, &err));

  CHECK(g_rec.game_registered);
  CHECK_EQ(g_rec.exe_windows, "minimal.exe");  /* default: game_binary */
  CHECK_EQ(g_rec.nexus_domain, "minimalgame"); /* lowercased game_id */
  CHECK(g_rec.steam_appid == 0);
  CHECK(!has_hook("steam_appid")); /* 0 = not on Steam = omitted */

  CHECK(g_rec.plugin_registered);
  CHECK_EQ(g_rec.plugin_description, "Adds basic support for game Minimal Game.");

  CHECK_EQ(g_rec.categories.size(), size_t{1});
  CHECK_EQ(g_rec.categories[0], "Game Support");

  /* Default tabs: Mods + Downloads, no Nexus wiring (no nexus_domain). */
  CHECK_EQ(g_rec.tabs.size(), size_t{2});
  if (g_rec.tabs.size() == 2) {
    CHECK_EQ(g_rec.tabs[0].first, "mods");
    CHECK_EQ(g_rec.tabs[0].second, "mods/");
    CHECK_EQ(g_rec.tabs[1].first, "downloads");
  }

  /* Defaults-derived hooks. */
  const std::string *execs = find_hook("executables");
  CHECK(execs && *execs == "minimal.exe");
  const std::string *localappdata = find_hook("localappdata_folder");
  CHECK(localappdata && *localappdata == "Minimal Game");
  const std::string *mygames = find_hook("mygames_folder");
  CHECK(mygames && *mygames == "Minimal Game");
  const std::string *deploy = find_hook("deploy_prefix");
  CHECK(deploy && *deploy == "mods");
  const std::string *cs = find_hook("case_sensitive");
  CHECK(cs && *cs == "false");

  /* Unset hooks are omitted, never placeholders. */
  CHECK(!has_hook("save_extensions"));
  CHECK(!has_hook("game_native_plugins"));
  CHECK(!has_hook("disable_mechanism"));
  CHECK(!has_hook("deploy_include_mod_id"));
  CHECK(!g_rec.order_encoding_registered);
}

void overrides_win() {
  reset();
  GmmRegistrationCtxV2 ctx = make_stub_ctx();
  OverridingGame game;
  std::string err;
  CHECK(game.register_all(&ctx, &err));

  CHECK_EQ(g_rec.nexus_domain, "overridinggame");
  CHECK(g_rec.steam_appid == 4242);
  const std::string *appid = find_hook("steam_appid");
  CHECK(appid && *appid == "4242");
  CHECK_EQ(g_rec.plugin_description, "Custom description.");
  const std::string *cs = find_hook("case_sensitive");
  CHECK(cs && *cs == "true");
  const std::string *exts = find_hook("save_extensions");
  CHECK(exts && *exts == "sav");
  /* extra() hooks land. */
  const std::string *ignored = find_hook("ignored_files");
  CHECK(ignored && *ignored == ".git,Thumbs.db");
}

void extra_hooks_run() {
  reset();
  GmmRegistrationCtxV2 ctx = make_stub_ctx();
  OverridingGame game;
  std::string err;
  CHECK(game.register_all(&ctx, &err));
  /* extra() runs last: the descriptor hooks come first. */
  CHECK(!g_rec.hooks.empty());
  CHECK_EQ(g_rec.hooks.back().first, "ignored_files");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: test_basic_game <case>\n");
    return 2;
  }
  const std::string casename = argv[1];
  if (casename == "missing_required_refuses")
    missing_required_refuses();
  else if (casename == "missing_required_names_first")
    missing_required_names_first();
  else if (casename == "defaults_apply")
    defaults_apply();
  else if (casename == "overrides_win")
    overrides_win();
  else if (casename == "extra_hooks_run")
    extra_hooks_run();
  else {
    std::fprintf(stderr, "unknown case: %s\n", casename.c_str());
    return 2;
  }

  if (g_failures > 0) {
    std::fprintf(stderr, "%s: %d failure(s)\n", casename.c_str(), g_failures);
    return 1;
  }
  return 0;
}
