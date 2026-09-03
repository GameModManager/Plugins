/**
 * Isaac Game Module - The Binding of Isaac: Rebirth
 *
 * Second ABI consumer.
 * Validates the ABI generalizes beyond Skyrim SE.
 * Isaac uses folder-based load order
 * (rename to reorder) and metadata.xml
 * for mod metadata. Structurally opposite to
 * Skyrim's plugins.txt.
 *
 * Game-dependent features registered via hooks:
 *   -
 * conflict_extensions: .png, .anm2, .wav, .lua
 *   - ignored_files: .git, __pycache__,
 * metadata.xml, disable.it, .DS_Store,
 *                     Thumbs.db, desktop.ini,
 * .Trashes, .Spotlight-V100,
 *                     $RECYCLE.BIN, .directory, ~
 *   -
 * disable_mechanism: disable.it
 *   - delayed_disable: true (defer sentinel writes
 * until Run/deploy phase)
 *   - workshop_id_pattern: _(\d+)$
 *   - auto_sort_groups:
 * framework(0), libraries(10), content(20), tweaks(30),
 * overrides(35), graphics(40),
 * music(45), late(50),
 *                        unknown(99)
 *   -
 * workshop_tag_categories: Steam Workshop tag -> category ID mapping
 *
 * Categories
 * (22, from Steam Workshop tags):
 *   Items > Active Items, Trinkets, Pills, Cards,
 * Pickups
 *   Lua, Player Characters, Familiars, Babies, Enemies, Bosses, Hazards
 *
 * Rooms > Floors, Challenges, Tweaks, Removals
 *   Graphics > Shaders, Sound Effects >
 * Music
 *
 * Capabilities: mods, downloads
 */

#include "plugin.hpp"

#include <cstdio>
#include <cstring>

/* -- Identity -- */
static const uint32_t STEAM_APPID = 250900;

/* -- Isaac conflict-relevant file extensions -- */
static const char* CONFLICT_EXTENSIONS = ".png,.anm2,.wav,.lua";

/* -- Files to ignore during conflict scanning -- */
static const char* IGNORED_FILES = ".git,__pycache__,metadata.xml,disable.it,.DS_Store,"
                                   "Thumbs.db,desktop.ini,.Trashes,.Spotlight-V100,"
                                   "$RECYCLE.BIN,.directory,~";

/* -- Workshop ID extraction pattern: folder names end with _<digits> -- */
static const char* WORKSHOP_ID_PATTERN = "_(\\d+)$";

/* -- Disable mechanism filename -- */
static const char* DISABLE_MECHANISM = "disable.it";

/* -- Metadata format: Isaac uses metadata.xml with <name> and <version> -- */
static const char* METADATA_FILE        = "metadata.xml";
static const char* METADATA_NAME_TAG    = "name";
static const char* METADATA_VERSION_TAG = "version";

/* -- Priority encoding: NNN prefix in <name> tag (e.g. "001 My Mod") -- */
static const char* PRIORITY_PREFIX_RE = "^[^a-zA-Z]+";
static const char* PRIORITY_FORMAT    = "%03d ";

/* -- Auto-sort group definitions (name:priority) --
 * Lower priority number = loads
 * earlier. Groups are sorted by priority,
 * then topological sort within each group by
 * before/after constraints.
 *
 * Matching priority (first match wins):
 *   1. id -
 * Steam Workshop ID (number after '_' in folder name)
 *   2. pattern - regex matched
 * against the full folder name
 *   3. tag     - matched against <tag id="..."> in
 * metadata.xml
 *   4. name    - matched against <name> in metadata.xml (least
 * reliable)
 */
static const char* AUTO_SORT_GROUPS = "["
                                      "{\"name\":\"framework\",\"priority\":0},"
                                      "{\"name\":\"libraries\",\"priority\":10},"
                                      "{\"name\":\"content\",\"priority\":20},"
                                      "{\"name\":\"tweaks\",\"priority\":30},"
                                      "{\"name\":\"overrides\",\"priority\":35},"
                                      "{\"name\":\"graphics\",\"priority\":40},"
                                      "{\"name\":\"music\",\"priority\":45},"
                                      "{\"name\":\"late\",\"priority\":50},"
                                      "{\"name\":\"unknown\",\"priority\":99}"
                                      "]";

/* -- Game versions: Isaac version string -> release date -- */
static const char* GAME_VERSIONS = "{"
                                   "\"1.9.716\":\"2026-04-11\","
                                   "\"1.9.713\":\"2025-11-10\","
                                   "\"1.9.711\":\"2025-03-01\","
                                   "\"1.9.710\":\"2026-01-18\","
                                   "\"1.9.79\":\"2025-01-12\","
                                   "\"1.9.78\":\"2024-12-17\","
                                   "\"1.9.76\":\"2024-11-26\","
                                   "\"1.9.74\":\"2024-11-19\","
                                   "\"1.9.5\":\"2024-01-03\","
                                   "\"1.7.9\":\"2022-12-08\""
                                   "}";

/* -- Order encoding: writes metadata.xml (Isaac's load order format) --
 * Isaac
 * determines load order from the mods folder itself (alphabetical/
 * renamed order),
 * but metadata.xml is also written so external tools and
 * the "Apply Sort Order"
 * feature can persist the intended priority.
 */
static int isaac_order_encoding(const char* const* ordered_mod_ids, size_t count,
                                const char* output_path, void* user_data)
{
  FILE* f = fopen(output_path, "w");
  if (!f)
    return 0;

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f, "<mods>\n");
  for (size_t i = 0; i < count; i++) {
    fprintf(f, "  <mod id=\"%s\" priority=\"%zu\" />\n", ordered_mod_ids[i], i);
  }
  fprintf(f, "</mods>\n");

  fclose(f);
  (void)user_data;
  return 1;
}

/* =========================================================================
 * Isaac
 * mod categories (from Steam Workshop tags)
 *
 * IDs start at 1000 to avoid collision
 * with MO2's default list (max 58).
 * Parent/child hierarchy mirrors the natural
 * grouping of Isaac mod types:
 *   Items > Active Items, Trinkets, Pills, Cards,
 * Pickups
 *   Rooms > Floors
 *   Graphics > Shaders
 *   Sound Effects > Music
 *
 * ========================================================================= */
static const int CAT_ITEMS         = 1000;
static const int CAT_ACTIVE_ITEMS  = 1001;
static const int CAT_TRINKETS      = 1002;
static const int CAT_PILLS         = 1003;
static const int CAT_CARDS         = 1004;
static const int CAT_PICKUPS       = 1005;
static const int CAT_LUA           = 1006;
static const int CAT_ROOMS         = 1007;
static const int CAT_FLOORS        = 1008;
static const int CAT_PLAYER_CHARS  = 1009;
static const int CAT_FAMILIARS     = 1010;
static const int CAT_BABIES        = 1011;
static const int CAT_ENEMIES       = 1012;
static const int CAT_GRAPHICS      = 1013;
static const int CAT_SHADERS       = 1014;
static const int CAT_SOUND_EFFECTS = 1015;
static const int CAT_MUSIC         = 1016;
static const int CAT_BOSSES        = 1017;
static const int CAT_HAZARDS       = 1018;
static const int CAT_CHALLENGES    = 1019;
static const int CAT_TWEAKS        = 1020;
static const int CAT_REMOVALS      = 1021;

/* -- Steam Workshop tag -> category mapping (JSON) --
 * Maps lowercased Steam Workshop
 * tag strings to internal category IDs.
 * Used by the engine to auto-assign categories
 * to mods fetched from
 * the Workshop based on their declared tags.
 */
static const char* WORKSHOP_TAG_CATEGORIES = "{"
                                             "\"lua\":"
                                             "1006,"
                                             "\"items\":"
                                             "1000,"
                                             "\"active items\":"
                                             "1001,"
                                             "\"trinkets\":"
                                             "1002,"
                                             "\"pills\":"
                                             "1003,"
                                             "\"cards\":"
                                             "1004,"
                                             "\"pickups\":"
                                             "1005,"
                                             "\"rooms\":"
                                             "1007,"
                                             "\"floors\":"
                                             "1008,"
                                             "\"player characters\":"
                                             "1009,"
                                             "\"familiars\":"
                                             "1010,"
                                             "\"babies\":"
                                             "1011,"
                                             "\"enemies\":"
                                             "1012,"
                                             "\"graphics\":"
                                             "1013,"
                                             "\"shaders\":"
                                             "1014,"
                                             "\"sound effects\":"
                                             "1015,"
                                             "\"music\":"
                                             "1016,"
                                             "\"bosses\":"
                                             "1017,"
                                             "\"hazards\":"
                                             "1018,"
                                             "\"challenges\":"
                                             "1019,"
                                             "\"tweaks\":"
                                             "1020,"
                                             "\"removals\":"
                                             "1021"
                                             "}";

/* =========================================================================
 *
 * Registration entry point
 *
 * ========================================================================= */
extern "C" void gmm_register_v2(GmmRegistrationCtxV2* raw)
{
  if (!raw)
    return;

  gmm::Ctx ctx{raw};

  /* Game identity - Isaac: Rebirth (Steam appid 250900) */
  ctx.game({"TheBindingOfIsaacRebirth", "The Binding of Isaac: Rebirth", STEAM_APPID,
            nullptr, /* gog_id */
            nullptr, /* epic_namespace */
            "thebindingofisaacrebirth", "isaac-ng.exe", nullptr, nullptr})
      /* Plugin metadata */
      .plugin({"The Binding of Isaac: Rebirth", "GameModManager Team", "1.0.0",
               "The Binding of Isaac: Rebirth game support "
               "(metadata.xml load order, resources/ layout)"})
      /* Category */
      .category("Game Support")
      /* Isaac mod categories (22 Steam Workshop-derived) */
      .categories({
          {CAT_ITEMS, "Items", 0},
          {CAT_ACTIVE_ITEMS, "Active Items", CAT_ITEMS},
          {CAT_TRINKETS, "Trinkets", CAT_ITEMS},
          {CAT_PILLS, "Pills", CAT_ITEMS},
          {CAT_CARDS, "Cards", CAT_ITEMS},
          {CAT_PICKUPS, "Pickups", CAT_ITEMS},
          {CAT_LUA, "Lua", 0},
          {CAT_ROOMS, "Rooms", 0},
          {CAT_FLOORS, "Floors", CAT_ROOMS},
          {CAT_PLAYER_CHARS, "Player Characters", 0},
          {CAT_FAMILIARS, "Familiars", 0},
          {CAT_BABIES, "Babies", 0},
          {CAT_ENEMIES, "Enemies", 0},
          {CAT_GRAPHICS, "Graphics", 0},
          {CAT_SHADERS, "Shaders", CAT_GRAPHICS},
          {CAT_SOUND_EFFECTS, "Sound Effects", 0},
          {CAT_MUSIC, "Music", CAT_SOUND_EFFECTS},
          {CAT_BOSSES, "Bosses", 0},
          {CAT_HAZARDS, "Hazards", 0},
          {CAT_CHALLENGES, "Challenges", 0},
          {CAT_TWEAKS, "Tweaks", 0},
          {CAT_REMOVALS, "Removals", 0},
      })
      /* Order encoding */
      .order_encoding(isaac_order_encoding)
      /* Tabs */
      .tabs(
          {{"mods", "Mods", "mods/",
            "Isaac mod folders (rename to reorder, disable.it to toggle)", nullptr,
            nullptr, nullptr, nullptr, nullptr},
           {"downloads", "Downloads", "mods/",
            "Download mods from Steam Workshop or Nexus Mods", "nxm", "nexusmods.com",
            "nexus", nullptr, nullptr},
           {"conflicts", "Conflicts", "mods/", "File conflicts between installed mods",
            nullptr, nullptr, nullptr, nullptr, "data"}})
      /* Game-dependent hooks */
      .hook("conflict_extensions", CONFLICT_EXTENSIONS)
      .hook("ignored_files", IGNORED_FILES)
      .hook("disable_mechanism", DISABLE_MECHANISM)
      .hook("delayed_disable", "true")
      .hook("metadata_file", METADATA_FILE)
      .hook("metadata_name_tag", METADATA_NAME_TAG)
      .hook("metadata_version_tag", METADATA_VERSION_TAG)
      .hook("priority_prefix_re", PRIORITY_PREFIX_RE)
      .hook("priority_format", PRIORITY_FORMAT)
      .hook("workshop_id_pattern", WORKSHOP_ID_PATTERN)
      .hook("auto_sort_groups", AUTO_SORT_GROUPS)
      .hook("workshop_tag_categories", WORKSHOP_TAG_CATEGORIES)
      .hook("masterlist_url",
            "https://raw.githubusercontent.com/GameModManager/Masterlist/"
            "refs/heads/main/games/thebindingofisaacrebirth/masterlist.yaml")
      .hook("steam_app_name", "The Binding of Isaac Rebirth")
      .hook("mods_subpath", "mods")
      .hook("deploy_prefix", "mods")
#ifdef __APPLE__
      .hook("game_mods_dir",
            "~/Library/Application Support/Binding of Isaac Afterbirth+ Mods")
#else
      .hook("game_mods_dir", "mods")
#endif
      .hook("deploy_include_mod_id", "true")
      .hook("game_versions", GAME_VERSIONS)
      .hook("executables", "REPENTOGONLauncher/REPENTOGONLauncher.exe,isaac-ng.exe,"
                           "The Binding of Isaac Rebirth.app")
      .hook("steam_appid", "250900")
      .hook("game_icon_url", "https://cdn2.steamgriddb.com/icon/"
                             "bc573864331a9e42e4511de6f678aa83/4/128x128.png")
      .hook("download_sources", "Nexus Mods,Steam")
      .hook("mod_counter_label", "Mods")
      .hook("conflict_order_reversed", "true")
      .hook("conflict_scan_dirs", "resources,resources-dlc3")
      .hook("mod_valid_dirs", "resources,resources-dlc3")
      .hook("uses_merged", "true");
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void)
{
  return GMM_ABI_VERSION;
}
