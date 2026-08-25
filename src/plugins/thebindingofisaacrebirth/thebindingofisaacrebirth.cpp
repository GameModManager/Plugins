/**
 * Isaac Game Module — The Binding of Isaac: Rebirth
 *
 * Second ABI consumer. Validates the ABI generalizes beyond Skyrim SE.
 * Isaac uses folder-based load order (rename to reorder) and metadata.xml
 * for mod metadata. Structurally opposite to Skyrim's plugins.txt.
 *
 * Game-dependent features registered via hooks:
 *   - conflict_extensions: .png, .anm2, .wav, .lua
 *   - ignored_files: .git, __pycache__, metadata.xml, disable.it, .DS_Store,
 *                     Thumbs.db, desktop.ini, .Trashes, .Spotlight-V100,
 *                     $RECYCLE.BIN, .directory, ~
 *   - disable_mechanism: disable.it
 *   - delayed_disable: true (defer sentinel writes until Run/deploy phase)
 *   - workshop_id_pattern: _(\d+)$
 *   - auto_sort_groups: framework(0), libraries(10), content(20), tweaks(30),
 *                        overrides(35), graphics(40), music(45), late(50), unknown(99)
 *   - workshop_tag_categories: Steam Workshop tag -> category ID mapping
 *
 * Categories (22, from Steam Workshop tags):
 *   Items > Active Items, Trinkets, Pills, Cards, Pickups
 *   Lua, Player Characters, Familiars, Babies, Enemies, Bosses, Hazards
 *   Rooms > Floors, Challenges, Tweaks, Removals
 *   Graphics > Shaders, Sound Effects > Music
 *
 * Capabilities: mods, downloads
 */

#include "gmm_abi_v1.h"

#include <cstdio>
#include <cstring>

/* -- Identity -- */
static const uint32_t STEAM_APPID = 250900;
static const char* NEXUS_DOMAIN = "thebindingofisaacrebirth";

/* -- Isaac conflict-relevant file extensions -- */
static const char* CONFLICT_EXTENSIONS = ".png,.anm2,.wav,.lua";

/* -- Files to ignore during conflict scanning -- */
static const char* IGNORED_FILES =
    ".git,__pycache__,metadata.xml,disable.it,.DS_Store,"
    "Thumbs.db,desktop.ini,.Trashes,.Spotlight-V100,"
    "$RECYCLE.BIN,.directory,~";

/* -- Workshop ID extraction pattern: folder names end with _<digits> -- */
static const char* WORKSHOP_ID_PATTERN = "_(\\d+)$";

/* -- Disable mechanism filename -- */
static const char* DISABLE_MECHANISM = "disable.it";

/* -- Metadata format: Isaac uses metadata.xml with <name> and <version> -- */
static const char* METADATA_FILE     = "metadata.xml";
static const char* METADATA_NAME_TAG = "name";
static const char* METADATA_VERSION_TAG = "version";

/* -- Priority encoding: NNN prefix in <name> tag (e.g. "001 My Mod") -- */
static const char* PRIORITY_PREFIX_RE = "^[^a-zA-Z]+";
static const char* PRIORITY_FORMAT    = "%03d ";

/* -- Auto-sort group definitions (name:priority) --
 * Lower priority number = loads earlier. Groups are sorted by priority,
 * then topological sort within each group by before/after constraints.
 *
 * Matching priority (first match wins):
 *   1. id      — Steam Workshop ID (number after '_' in folder name)
 *   2. pattern — regex matched against the full folder name
 *   3. tag     — matched against <tag id="..."> in metadata.xml
 *   4. name    — matched against <name> in metadata.xml (least reliable)
 */
static const char* AUTO_SORT_GROUPS =
    "["
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
static const char* GAME_VERSIONS =
    "{"
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
 * Isaac determines load order from the mods folder itself (alphabetical/
 * renamed order), but metadata.xml is also written so external tools and
 * the "Apply Sort Order" feature can persist the intended priority.
 */
static int isaac_order_encoding(const char* const* ordered_mod_ids,
                                size_t count,
                                const char* output_path,
                                void* user_data) {
    FILE* f = fopen(output_path, "w");
    if (!f) return 0;

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
 * Isaac mod categories (from Steam Workshop tags)
 *
 * IDs start at 1000 to avoid collision with MO2's default list (max 58).
 * Parent/child hierarchy mirrors the natural grouping of Isaac mod types:
 *   Items > Active Items, Trinkets, Pills, Cards, Pickups
 *   Rooms > Floors
 *   Graphics > Shaders
 *   Sound Effects > Music
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

static const int IsaacCategoryIds[] = {
    CAT_ITEMS, CAT_ACTIVE_ITEMS, CAT_TRINKETS, CAT_PILLS, CAT_CARDS,
    CAT_PICKUPS, CAT_LUA, CAT_ROOMS, CAT_FLOORS, CAT_PLAYER_CHARS,
    CAT_FAMILIARS, CAT_BABIES, CAT_ENEMIES, CAT_GRAPHICS, CAT_SHADERS,
    CAT_SOUND_EFFECTS, CAT_MUSIC, CAT_BOSSES, CAT_HAZARDS, CAT_CHALLENGES,
    CAT_TWEAKS, CAT_REMOVALS,
};

static const char* const IsaacCategoryNames[] = {
    "Items", "Active Items", "Trinkets", "Pills", "Cards",
    "Pickups", "Lua", "Rooms", "Floors", "Player Characters",
    "Familiars", "Babies", "Enemies", "Graphics", "Shaders",
    "Sound Effects", "Music", "Bosses", "Hazards", "Challenges",
    "Tweaks", "Removals",
};

/* Parent IDs: 0 = root, positive = parent category.
 * Items is parent of Active Items, Trinkets, Pills, Cards, Pickups.
 * Rooms is parent of Floors.
 * Graphics is parent of Shaders.
 * Sound Effects is parent of Music. */
static const int IsaacCategoryParentIds[] = {
    0,                  /* Items */
    CAT_ITEMS,          /* Active Items -> Items */
    CAT_ITEMS,          /* Trinkets -> Items */
    CAT_ITEMS,          /* Pills -> Items */
    CAT_ITEMS,          /* Cards -> Items */
    CAT_ITEMS,          /* Pickups -> Items */
    0,                  /* Lua */
    0,                  /* Rooms */
    CAT_ROOMS,          /* Floors -> Rooms */
    0,                  /* Player Characters */
    0,                  /* Familiars */
    0,                  /* Babies */
    0,                  /* Enemies */
    0,                  /* Graphics */
    CAT_GRAPHICS,       /* Shaders -> Graphics */
    0,                  /* Sound Effects */
    CAT_SOUND_EFFECTS,  /* Music -> Sound Effects */
    0,                  /* Bosses */
    0,                  /* Hazards */
    0,                  /* Challenges */
    0,                  /* Tweaks */
    0,                  /* Removals */
};

static const size_t IsaacCategoryCount =
    sizeof(IsaacCategoryIds) / sizeof(IsaacCategoryIds[0]);

/* -- Steam Workshop tag -> category mapping (JSON) --
 * Maps lowercased Steam Workshop tag strings to internal category IDs.
 * Used by the engine to auto-assign categories to mods fetched from
 * the Workshop based on their declared tags.
 */
static const char* WORKSHOP_TAG_CATEGORIES =
    "{"
    "\"lua\":"               "1006,"
    "\"items\":"             "1000,"
    "\"active items\":"      "1001,"
    "\"trinkets\":"          "1002,"
    "\"pills\":"             "1003,"
    "\"cards\":"             "1004,"
    "\"pickups\":"           "1005,"
    "\"rooms\":"             "1007,"
    "\"floors\":"            "1008,"
    "\"player characters\":" "1009,"
    "\"familiars\":"         "1010,"
    "\"babies\":"            "1011,"
    "\"enemies\":"           "1012,"
    "\"graphics\":"          "1013,"
    "\"shaders\":"           "1014,"
    "\"sound effects\":"     "1015,"
    "\"music\":"             "1016,"
    "\"bosses\":"            "1017,"
    "\"hazards\":"           "1018,"
    "\"challenges\":"        "1019,"
    "\"tweaks\":"            "1020,"
    "\"removals\":"          "1021"
    "}";

/* =========================================================================
 * Registration entry point
 * ========================================================================= */
extern "C" {

uint32_t gmm_abi_version() {
    return GMM_ABI_VERSION;
}

void gmm_register_v1(GmmRegistrationCtx* ctx) {
    /* -- Identity -- Isaac: Rebirth (Steam appid 250900) */
    ctx->register_identity(ctx,
        STEAM_APPID,
        NULL,                   /* gog_id */
        NULL,                   /* epic_namespace */
        NEXUS_DOMAIN,
        "The Binding of Isaac: Rebirth",
        "isaac-ng.exe",         /* exe_windows */
        NULL,                   /* exe_linux */
        NULL                    /* exe_macos */
    );

    /* -- Metadata for the Plugins settings tab -- */
    if (ctx->register_meta) {
        ctx->register_meta(ctx,
            "GameModManager Team",
            "1.0.0",
            "The Binding of Isaac: Rebirth game support "
            "(metadata.xml load order, resources/ layout)");
    }

    if (ctx->register_category) {
        ctx->register_category(ctx, "Game Support");
    }

    /* -- Isaac mod categories (22 Steam Workshop-derived) --
     * Parent/child hierarchy: Items > Active Items/Trinkets/etc.,
     * Rooms > Floors, Graphics > Shaders, Sound Effects > Music. */
    if (ctx->register_categories) {
        ctx->register_categories(ctx,
            IsaacCategoryIds,
            IsaacCategoryNames,
            IsaacCategoryParentIds,
            IsaacCategoryCount);
    }

    /* -- Order encoding hook -- metadata.xml format */
    ctx->register_order_encoding(ctx, isaac_order_encoding);

    /* -- Tabs -- */
    ctx->register_tab(ctx,
        "mods", "Mods", "mods/",
        "Isaac mod folders (rename to reorder, disable.it to toggle)",
        NULL, NULL, NULL, NULL, NULL);

    ctx->register_tab(ctx,
        "downloads", "Downloads", "mods/",
        "Download mods from Steam Workshop or Nexus Mods",
        "nxm", "nexusmods.com", "nexus", NULL, NULL);

    ctx->register_tab(ctx,
        "conflicts", "Conflicts", "mods/",
        "File conflicts between installed mods",
        NULL, NULL, NULL, NULL, "data");

    /* -- Game-dependent hooks -- */

    /* Conflict extensions -- Isaac's asset/script formats */
    ctx->register_hook(ctx,
        "conflict_extensions", CONFLICT_EXTENSIONS,
        NULL, 0, NULL);

    /* Ignored files -- skip during conflict scanning */
    ctx->register_hook(ctx,
        "ignored_files", IGNORED_FILES,
        NULL, 0, NULL);

    /* Disable mechanism -- Isaac checks for this file to skip loading */
    ctx->register_hook(ctx,
        "disable_mechanism", DISABLE_MECHANISM,
        NULL, 0, NULL);

    /* Delayed disable -- defer disable.it writes until Run/deploy phase.
     * Isaac's Direct deploy mode must not touch the game dir on toggle:
     * the sentinel is reconciled from the profile at launch instead. */
    ctx->register_hook(ctx,
        "delayed_disable", "true",
        NULL, 0, NULL);

    /* Metadata file -- which file to read for mod name/version */
    ctx->register_hook(ctx,
        "metadata_file", METADATA_FILE,
        NULL, 0, NULL);

    /* Metadata name tag -- XML tag containing the mod display name */
    ctx->register_hook(ctx,
        "metadata_name_tag", METADATA_NAME_TAG,
        NULL, 0, NULL);

    /* Metadata version tag -- XML tag containing the mod version */
    ctx->register_hook(ctx,
        "metadata_version_tag", METADATA_VERSION_TAG,
        NULL, 0, NULL);

    /* Priority prefix regex -- strip this from display name */
    ctx->register_hook(ctx,
        "priority_prefix_re", PRIORITY_PREFIX_RE,
        NULL, 0, NULL);

    /* Priority format -- printf format for the NNN prefix */
    ctx->register_hook(ctx,
        "priority_format", PRIORITY_FORMAT,
        NULL, 0, NULL);

    /* Workshop ID pattern -- extract from folder name suffix _<digits> */
    ctx->register_hook(ctx,
        "workshop_id_pattern", WORKSHOP_ID_PATTERN,
        NULL, 0, NULL);

    /* Auto-sort group definitions -- JSON array of {name, priority} */
    ctx->register_hook(ctx,
        "auto_sort_groups", AUTO_SORT_GROUPS,
        NULL, 0, NULL);

    /* Workshop tag -> category mapping -- JSON {lowercase_tag: category_id}.
     * The engine uses this to auto-assign categories to mods fetched from
     * the Steam Workshop based on their declared tags. */
    ctx->register_hook(ctx,
        "workshop_tag_categories", WORKSHOP_TAG_CATEGORIES,
        NULL, 0, NULL);

    /* Masterlist URL -- community-maintained dependency rules */
    ctx->register_hook(ctx,
        "masterlist_url",
        "https://raw.githubusercontent.com/GameModManager/Masterlist/"
        "refs/heads/main/games/thebindingofisaacrebirth/masterlist.yaml",
        NULL, 0, NULL);

    /* Steam app name -- for VDF detection and folder resolution */
    ctx->register_hook(ctx,
        "steam_app_name", "The Binding of Isaac Rebirth",
        NULL, 0, NULL);

    /* Mods subfolder -- relative to game install root */
    ctx->register_hook(ctx,
        "mods_subpath", "mods",
        NULL, 0, NULL);

    /* Deploy prefix -- game-relative path prefix for deployed mod files.
     * Mod content inside staging_dir gets placed under this subpath.
     * Isaac deploys to "mods" (game_dir/mods/ModName/...). */
    ctx->register_hook(ctx,
        "deploy_prefix", "mods",
        NULL, 0, NULL);

    /* Deploy include mod id -- whether to include the mod folder name in the
     * deploy target path. Isaac mods go into mods/ModName/ (true).
     * Skyrim-style mods go directly into Data/ (false, default). */
    ctx->register_hook(ctx,
        "deploy_include_mod_id", "true",
        NULL, 0, NULL);

    /* Game versions -- Isaac version string -> release date */
    ctx->register_hook(ctx,
        "game_versions", GAME_VERSIONS,
        NULL, 0, NULL);

    /* Executables -- relative paths to all launchable binaries.
     * Isaac ships as a Windows game; all executables are .exe files.
     * The launcher auto-detects runtime from file extension:
     *   .exe -> Proton, otherwise -> native.
     * REPENTOGONLauncher is the preferred entry point (mod loader).
     * Listed in priority order: first = default if found. The macOS Steam
     * release is "The Binding of Isaac Rebirth.app"; the Core-side game-dir
     * scan (Workspace-6su) drops it on Windows/Linux where it does not
     * exist, so declaring it here needs no per-platform split. */
    ctx->register_hook(ctx,
        "executables",
        "REPENTOGONLauncher/REPENTOGONLauncher.exe,isaac-ng.exe,"
        "The Binding of Isaac Rebirth.app",
        NULL, 0, NULL);

    /* Steam appid -- used by ProtonRuntime to find the correct per-game
     * Proton version from Steam's config.vdf. Also used by platform
     * detection to find the game's Steam install directory. */
    ctx->register_hook(ctx,
        "steam_appid", "250900",
        NULL, 0, NULL);

    /* Declared game icon -- SteamGridDB CDN asset. The engine downloads it
     * into its global icon cache (~/.local/share/GameModManager/cache/icons)
     * on first use and enforces the UI's target size when rendering. */
    ctx->register_hook(ctx,
        "game_icon_url",
        "https://cdn2.steamgriddb.com/icon/"
        "bc573864331a9e42e4511de6f678aa83/4/128x128.png",
        NULL, 0, NULL);

    /* Download sources -- comma-separated list of source names for status bar */
    ctx->register_hook(ctx,
        "download_sources", "Nexus,Steam",
        NULL, 0, NULL);

    /* Mod counter label -- how to label the mod count in status bar.
     * "Mods" = raw mod folders, "Plugins" = ESM/ESP-style plugins. */
    ctx->register_hook(ctx,
        "mod_counter_label", "Mods",
        NULL, 0, NULL);

    /* Conflict order reversed -- Isaac resolves conflicts top-to-bottom:
     * the mod at the top of the list wins file conflicts (loads first).
     * Priority numbering is NOT inverted; only the conflict resolution
     * direction changes. The Overwrite mod (priority 0) sits at the top. */
    ctx->register_hook(ctx,
        "conflict_order_reversed", "true",
        NULL, 0, NULL);

    /* Conflict scan dirs -- Isaac mods only conflict in these subdirectories;
     * anything outside is not read by the game engine. */
    ctx->register_hook(ctx,
        "conflict_scan_dirs", "resources,resources-dlc3",
        NULL, 0, NULL);

    /* Content-validity markers: top-level folders that count as real Isaac
     * mod data. A mod folder without resources/, resources-dlc3/ - and without
     * metadata.xml (handled by the engine's metadata-presence rule) - is
     * flagged "No valid game data" in the mod list. */
    ctx->register_hook(ctx,
        "mod_valid_dirs", "resources,resources-dlc3",
        NULL, 0, NULL);

    /* Uses merged pseudo-mod -- only Isaac pins the __merged__ row in the mod
     * list (merge-tool output landing zone). Other games don't use it. */
    ctx->register_hook(ctx,
        "uses_merged", "true",
        NULL, 0, NULL);
}

}  /* extern "C" */
