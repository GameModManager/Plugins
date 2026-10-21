# GameTemplate fill points

Every fill point in the copied files, in the order you will hit them.
`GameTemplate.cpp` carries the same markers inline (`FILL:` comments); this
file is the checklist view. Core consumer references are relative to
`projects/Core/src`.

## 1. Identity (game() call)

| Field | Fill with | Consumer |
| --- | --- | --- |
| game_id | Your game id. MUST equal the CMake target name (the .so stem): Core defaults an instance's game_id to the .so stem before register_game runs | plugin_loader.cpp |
| display_name | Human-readable name | UI |
| steam_appid | Steam appid, or 0 if not on Steam | app/core.cpp:714-727 (Steam VDF match) |
| gog_id / epic_namespace | Store ids, or nullptr | detection |
| nexus_domain | Nexus domain slug, or nullptr | downloads |
| exe_windows / exe_linux / exe_macos | exe names, or nullptr (runtime detection / unsupported) | launch |

## 2. Plugin metadata (plugin() call)

Fill name, author, description. Leave `VERSION` as the macro - it is the
GMM_VERSION define from the build (Core's project version); never hardcode
a number.

## 3. Tabs (tabs() call)

Pick the capabilities your game uses: plugins, archives, saves, downloads,
data, mods, conflicts. Fill data_path and description per tab; wire
protocol_handler/website_domain only for the downloads tab.

## 4. Hooks (the .hook() chain)

Fill every value, or delete the line if the aspect does not apply. Each
hook's comment cites its Core consumer. The two you must never skip:

- `game_native_plugins`: every file the base game ships (official plugins,
  DLC). Base-game filtering is a HARD CONSTRAINT - see
  knowledgebase/cpp/agent-rules.md.
- `steam_appid`: game detection.

Do not add a hook tag that is not already in the file without grepping Core
for a reader first: tags are stringly typed and a typo produces a silent
dead hook. `tools/check_hook_tags.sh` enforces this for the template; run
it against your copy too (see template/cmake_lifecycle.md).

## 5. OPTIONAL pieces

- **Order encoding** (`template_order_encoding` + `.order_encoding()`):
  replace the generic one-per-line writer with your game's real
  load-order format, or delete both if your game has no load-order file.
- **SaveAdapterStub.cpp**: off by default. Fill `parse_template()` with a
  call into your real packet, uncomment the `.save_parser()` /
  `.save_overlay()` lines in GameTemplate.cpp, and add the stub to
  add_game_plugin. Register under your own game_id FIRST (the Saves scan
  looks parsers up by instance game_id), then aliases. If your game needs
  no binary save parsing, delete the file and skip this step.
- **Categories / game variants / tool / loot hooks**: commented-out
  examples in GameTemplate.cpp; uncomment + fill only if they apply.

## 6. wine.json

Fill with the wine packages your game needs (see Skyrim SE's and Isaac's
for the shape). Add your game id to the wine.json foreach in the repo
CMakeLists.txt (template/cmake_lifecycle.md step 3).

## 7. Verbatim (do not touch)

- The `gmm::Ctx` fluent chain structure and `gmm_register_v2` /
  `gmm_abi_version` entry points.
- The null check at the top of `gmm_register_v2`.
- The `VERSION` macro in plugin().
- The category line `.category("Game Support")`.
- The ABI adapter helpers in SaveAdapterStub.cpp (str_dup / dup_opt /
  dup_str / fill_plugin_array / to_abi / parse_with) - only their FILL
  points change.
