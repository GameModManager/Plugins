# Game template - fill list

`template/game/` is the checked-in skeleton a new game-support plugin author
copies. Copy the whole directory to
`src/plugins/GameSupport/<YourGame>/`, then work through the fill points
(`template/game/README.md` is the per-file walkthrough; the .cpp files carry
inline `FILL:` comments).

## REPLACE (author fills)

- **game_id + CMake target name** - must be identical strings; Core defaults
  an instance's game_id to the .so stem.
- **steam_appid** (both the `game()` field and the `steam_appid` hook).
- **exe names** per OS in `game()`.
- **nexus_domain / gog_id / epic_namespace**.
- **display_name, author, description** in `plugin()`.
- **Tabs**: capabilities + data_path + descriptions.
- **Hook values**: every `.hook()` value is a fill-or-delete decision. The
  two hard ones: `game_native_plugins` (base-game filtering is a HARD
  CONSTRAINT) and `steam_appid` (detection).
- **Categories** (optional): uncomment the `.categories()` example and fill.
- **wine.json**: packages your game needs under wine.
- **Order-encoding writer** (optional): your game's real load-order format.
- **SaveAdapterStub.cpp** (optional, off by default): fill
  `parse_template()` with your real packet call, uncomment the
  `.save_parser()` / `.save_overlay()` lines, add the file to
  add_game_plugin. Delete it if your game needs no binary save parsing.

## VERBATIM (leave as-is)

- The `gmm::Ctx` fluent chain structure and the `gmm_register_v2` /
  `gmm_abi_version` entry points (including the null check).
- `VERSION` in `plugin()` - the GMM_VERSION define from the build (Core's
  project version). Never hardcode a version number.
- `.category("Game Support")`.
- The ABI adapter helpers in SaveAdapterStub.cpp (str_dup / dup_opt /
  dup_str / fill_plugin_array / to_abi / parse_with).
- The hook-tag SET: every tag in the template is verified live (Core reads
  it; each comment cites the consumer). Do not add a tag without grepping
  Core for a reader - `tools/check_hook_tags.sh` fails on tags Core does
  not read. Dead tags (mod_counter_label, steam_app_name, game_versions,
  auto_sort_groups) are excluded on purpose.
- No `gmm_abi_features` export: omitting it is the safe v2.0 baseline.

## Guard tests

`template.instance_builds` + `template.hook_tags_drift` (ctest, test builds
only) keep the template compiling and its tag table honest. See
`cmake_lifecycle.md` for what your copy inherits.
