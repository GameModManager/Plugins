# Game template - BasicGame

A new game-support plugin is a small subclass of `gmm::BasicGame`
(`src/shared/BasicGame/BasicGame.hpp`, header-only). The base class carries
the entire registration - identity, plugin metadata, category, default
tabs, and every common hook tag - with a documented default for every
field. This mirrors MO2's `modorganizer-basic_games` `BasicGame`: the heavy
interface is implemented once; a new game fills a descriptor and anything
untouched rides the default.

## How to add a game

1. Copy `template/game/MyGame.cpp` to
   `src/plugins/GameSupport/<YourGame>/<YourGame>.cpp`.
2. Rename the struct; fill the required fields; delete optional lines
   whose defaults already fit. Every field's default is documented inline
   in `BasicGame.hpp` next to the field.
3. Add the CMake block (`template/cmake_lifecycle.md`). `add_game_plugin`
   already puts `src/shared/BasicGame` on the include path.
4. Copy `template/game/wine.json` next to your .cpp and fill it; add your
   game id to the wine.json foreach in the repo `CMakeLists.txt`.

## Required fields

`game_id`, `display_name`, `game_binary`, `mods_subpath`. A subclass
missing any of them is refused at load with a loud stderr message naming
the field, and NOTHING is registered - a half-filled game can never
half-load (the `basic_game.*` ctest cases cover this).

`game_id` MUST equal the CMake target name (the .so stem): Core defaults
an instance's game_id to the .so stem before register_game runs.

## Defaults

- `nexus_domain`: game_id lowercased. When it resolves, the default
  Downloads tab is Nexus-wired (nxm protocol).
- `exe_windows`: `game_binary`. `exe_linux`/`exe_macos`: null (Proton /
  unsupported - the Skyrim pattern).
- `description`: "Adds basic support for game <display_name>."
- `author`: "GameModManager Team". `version`: always the VERSION build
  define - never a hardcoded number.
- `executables`: `game_binary`. `localappdata_folder` / `mygames_folder`:
  `display_name`. `deploy_prefix`: `mods_subpath`.
- An unset hook is OMITTED, never registered with a placeholder value: an
  absent tag beats a dead one.

## Rich-plugin extras (override virtuals)

- `tabs()` - default: Mods + Downloads. Override to replace the pair
  (Skyrim's five-tab layout).
- `order_encoding()` - default: none. Override to write your load-order
  file (Skyrim's plugins.txt writer, Isaac's metadata.xml writer).
- `extra(Ctx&)` - tools, game variants, save parsers, categories,
  workshop / loot / conflict / ignored-files hooks: anything with no
  descriptor field. Called last. Real examples:
  `SkyrimSpecialEdition.cpp` (tool + variants + save parser),
  `TheBindingOfIsaacRebirth.cpp` (categories + workshop hooks). For a
  binary save parser, copy the adapter pattern from
  `SkyrimSESaveParser.cpp` (malloc'd GmmSaveDataV2, engine frees the
  strings, no exceptions cross the ABI).

## Guard tests

- `template.instance_builds` - the checked-in MyGame example compiles as a
  real module in test builds and the .so is asserted (ABI churn
  detection).
- `template.hook_tags_drift` - greps Core for every hook tag BasicGame
  registers; fails listing tags Core does not read. The dead tags
  (mod_counter_label, steam_app_name, game_versions, auto_sort_groups)
  are excluded on purpose.
- `basic_game.*` - required-field refusal + defaults behavior.
