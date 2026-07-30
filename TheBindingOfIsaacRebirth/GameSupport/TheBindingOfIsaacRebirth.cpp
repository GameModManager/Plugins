/**
 * Isaac Game Module — The Binding of Isaac: Rebirth
 *
 * Second ABI consumer. Validates the ABI generalizes beyond Skyrim SE.
 * Isaac uses folder-based load order (rename to reorder) and metadata.xml
 * for mod metadata. Structurally opposite to Skyrim's plugins.txt.
 *
 * Game-dependent features registered via hooks:
 *   - conflict_extensions: .png, .anm2, .wav, .lua
 *   - ignored_files: .git, __pycache__, metadata.xml, disable.it
 *   - disable_mechanism: disable.it
 *   - workshop_id_pattern: _(\d+)$
 *   - auto_sort_groups: framework(0), libraries(10), content(20), tweaks(30),
 *                        overrides(35), graphics(40), music(45), late(50), unknown(99)
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
static const char* IGNORED_FILES = ".git,__pycache__,metadata.xml,disable.it";

/* -- Workshop ID extraction pattern: folder names end with _<digits> -- */
static const char* WORKSHOP_ID_PATTERN = "_(\\d+)$";

/* -- Disable mechanism filename -- */
static const char* DISABLE_MECHANISM = "disable.it";

/* -- Metadata format: Isaac uses metadata.xml with <name> and <version> tags -- */
static const char* METADATA_FILE = "metadata.xml";
static const char* METADATA_NAME_TAG = "name";
static const char* METADATA_VERSION_TAG = "version";

/* -- Priority encoding: NNN prefix in <name> tag (e.g. "001 My Mod") -- */
static const char* PRIORITY_PREFIX_RE = "^[^a-zA-Z]+";
static const char* PRIORITY_FORMAT = "%03d ";

/* -- Auto-sort group definitions (name:priority) --
 * Lower priority number = loads earlier. Groups are sorted by priority,
 * then topological sort within each group by before/after constraints.
 *
 * Matching priority (first match wins):
 *   1. id     — Steam Workshop ID (number after '_' in folder name)
 *   2. pattern — regex matched against the full folder name
 *   3. tag    — matched against <tag id="..."> in metadata.xml
 *   4. name   — matched against <name> in metadata.xml (least reliable)
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

/* -- Game versions: Isaac version string → release date -- */
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

/* -- Registration entry point -- */
extern "C" void gmm_register_v1(GmmRegistrationCtx* ctx) {
    /* Register identity — Isaac: Rebirth (Steam appid 250900) */
    ctx->register_identity(ctx,
        STEAM_APPID,
        NULL,           /* gog_id */
        NULL,           /* epic_namespace */
        NEXUS_DOMAIN,
        "The Binding of Isaac: Rebirth",
        NULL,           /* exe_windows */
        NULL,           /* exe_linux */
        NULL            /* exe_macos */
    );

    /* Register order encoding hook — metadata.xml format */
    ctx->register_order_encoding(ctx, isaac_order_encoding);

    /* Register tabs */
    ctx->register_tab(ctx,
        "mods",
        "Mods",
        "mods/",
        "Isaac mod folders (rename to reorder, disable.it to toggle)",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );

    ctx->register_tab(ctx,
        "downloads",
        "Downloads",
        "mods/",
        "Download mods from Steam Workshop or Nexus Mods",
        "nxm",
        "nexusmods.com",
        "nexus",
        NULL,
        NULL
    );

    ctx->register_tab(ctx,
        "conflicts",
        "Conflicts",
        "mods/",
        "File conflicts between installed mods",
        NULL,
        NULL,
        NULL,
        NULL,
        "data"
    );

    /* -- Game-dependent hooks -- */

    /* Conflict extensions — Isaac's asset/script formats */
    ctx->register_hook(ctx,
        "conflict_extensions",
        CONFLICT_EXTENSIONS,
        NULL, 0, NULL);

    /* Ignored files — skip during conflict scanning */
    ctx->register_hook(ctx,
        "ignored_files",
        IGNORED_FILES,
        NULL, 0, NULL);

    /* Disable mechanism — Isaac checks for this file to skip loading */
    ctx->register_hook(ctx,
        "disable_mechanism",
        DISABLE_MECHANISM,
        NULL, 0, NULL);

    /* Metadata file — which file to read for mod name/version */
    ctx->register_hook(ctx,
        "metadata_file",
        METADATA_FILE,
        NULL, 0, NULL);

    /* Metadata name tag — XML tag containing the mod display name */
    ctx->register_hook(ctx,
        "metadata_name_tag",
        METADATA_NAME_TAG,
        NULL, 0, NULL);

    /* Metadata version tag — XML tag containing the mod version */
    ctx->register_hook(ctx,
        "metadata_version_tag",
        METADATA_VERSION_TAG,
        NULL, 0, NULL);

    /* Priority prefix regex — strip this from display name */
    ctx->register_hook(ctx,
        "priority_prefix_re",
        PRIORITY_PREFIX_RE,
        NULL, 0, NULL);

    /* Priority format — printf format for the NNN prefix */
    ctx->register_hook(ctx,
        "priority_format",
        PRIORITY_FORMAT,
        NULL, 0, NULL);

    /* Workshop ID pattern — extract from folder name suffix _<digits> */
    ctx->register_hook(ctx,
        "workshop_id_pattern",
        WORKSHOP_ID_PATTERN,
        NULL, 0, NULL);

    /* Auto-sort group definitions — JSON array of {name, priority} */
    ctx->register_hook(ctx,
        "auto_sort_groups",
        AUTO_SORT_GROUPS,
        NULL, 0, NULL);

    /* Masterlist URL — community-maintained dependency rules */
    ctx->register_hook(ctx,
        "masterlist_url",
        "https://raw.githubusercontent.com/PetricaT/GameModManager-Masterlist/main/isaac/masterlist.yaml",
        NULL, 0, NULL);

    /* Steam app name — for VDF detection and folder resolution */
    ctx->register_hook(ctx,
        "steam_app_name",
        "The Binding of Isaac Rebirth",
        NULL, 0, NULL);

    /* Mods subfolder — relative to game install root */
    ctx->register_hook(ctx,
        "mods_subpath",
        "mods",
        NULL, 0, NULL);

    /* Deploy prefix — game-relative path prefix for deployed mod files.
     * Mod content inside staging_dir gets placed under this subpath.
     * Isaac deploys to "mods" (game_dir/mods/ModName/...). */
    ctx->register_hook(ctx,
        "deploy_prefix",
        "mods",
        NULL, 0, NULL);

    /* Deploy include mod id — whether to include the mod folder name in the
     * deploy target path. Isaac mods go into mods/ModName/ (true).
     * Skyrim-style mods go directly into Data/ (false, default). */
    ctx->register_hook(ctx,
        "deploy_include_mod_id",
        "true",
        NULL, 0, NULL);

    /* Game versions — Isaac version string → release date */
    ctx->register_hook(ctx,
        "game_versions",
        GAME_VERSIONS,
        NULL, 0, NULL);

    /* Executables — relative paths to all launchable binaries.
     * Isaac ships as a Windows game; all executables are .exe files.
     * The launcher auto-detects runtime from file extension:
     *   .exe → Proton, otherwise → native.
     * REPENTOGONLauncher is the preferred entry point (mod loader).
     * Listed in priority order: first = default if found. */
    ctx->register_hook(ctx,
        "executables",
        "REPENTOGONLauncher/REPENTOGONLauncher.exe,isaac-ng.exe",
        NULL, 0, NULL);

    /* Default executable — REPENTOGON mod launcher provides modding APIs. */
    ctx->register_hook(ctx,
        "default_executable",
        "REPENTOGONLauncher/REPENTOGONLauncher.exe",
        NULL, 0, NULL);

    /* Steam appid — used by ProtonRuntime to find the correct per-game
     * Proton version from Steam's config.vdf. Also used by platform
     * detection to find the game's Steam install directory. */
    ctx->register_hook(ctx,
        "steam_appid",
        "250900",
        NULL, 0, NULL);

    /* Download sources — comma-separated list of source names for status bar */
    ctx->register_hook(ctx,
        "download_sources",
        "Nexus,Steam",
        NULL, 0, NULL);

    /* Mod counter label — how to label the mod count in status bar.
     * "Mods" = raw mod folders, "Plugins" = ESM/ESP-style plugins. */
    ctx->register_hook(ctx,
        "mod_counter_label",
        "Mods",
        NULL, 0, NULL);

    /* Conflict order reversed — Isaac resolves conflicts top-to-bottom:
     * the mod at the top of the list wins file conflicts (loads first).
     * Priority numbering is NOT inverted; only the conflict resolution
     * direction changes. The Overwrite mod (priority 0) sits at the top. */
    ctx->register_hook(ctx,
        "conflict_order_reversed",
        "true",
        NULL, 0, NULL);
}

/* -- Version guard -- */
extern "C" uint32_t gmm_abi_version(void) {
    return GMM_ABI_VERSION;
}
