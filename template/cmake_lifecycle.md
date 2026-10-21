# CMake lifecycle for a new game plugin

The exact block to add to the repo `CMakeLists.txt` after copying
`template/game/` to `src/plugins/GameSupport/<YourGame>/`. This mirrors the
existing `add_game_plugin` pattern (Skyrim SE / Isaac).

## 1. Plugin target

```cmake
add_game_plugin(<YourGame>
    src/plugins/GameSupport/<YourGame>/<YourGame>.cpp
    # Only if you kept the optional save adapter:
    src/plugins/GameSupport/<YourGame>/SaveAdapterStub.cpp
)
```

`add_game_plugin` sets PREFIX "" and LIBRARY_OUTPUT_DIRECTORY
`build/plugins`, so the shipped artifact is `build/plugins/<YourGame>.so`.
The target name MUST equal the game_id in the plugin's `game()` call: Core
defaults an instance's game_id to the .so stem.

## 2. wine.json copy

Add your game id to the existing foreach so the file ships as
`build/plugins/<YourGame>/wine.json` (the UI reads it as "Recommended Wine
Packages"):

```cmake
foreach(GAME_ID SkyrimSpecialEdition TheBindingOfIsaacRebirth <YourGame>)
```

## 3. Version

Nothing to set: the plugin metadata version is the `VERSION` compile define
(`target_compile_definitions(... VERSION="${GMM_VERSION}")` inside
`add_game_plugin`). GMM_VERSION comes from Core's `project(VERSION ...)` -
one version source for the whole tree. Never hardcode a version string in
the plugin source.

## 4. Test case IDs (test builds only, GMM_BUILD_TESTS)

The template guard tests live in the repo CMakeLists `if(GMM_BUILD_TESTS)`
block:

- `template.instance_builds` - compiles the checked-in template exactly as
  a new author would (a `GameTemplate` MODULE target) and asserts the .so
  exists. Keeps the template TRUE after ABI churn. Your copy gets the same
  guarantee for free: it is built by the same `add_game_plugin` machinery,
  so an ABI break fails your build too.
- `template.hook_tags_drift` - runs `tools/check_hook_tags.sh`, which greps
  Core for every hook tag the template registers and fails listing tags
  Core does not read. Run it against your copy after adding or changing
  hooks:

  ```bash
  tools/check_hook_tags.sh ../Core/src   # from the repo root
  ctest -R template. -N                  # list the template guard tests
  ```

Both are ctest entries, not per-build steps: the tag check runs when the
test suite runs, not on every compile.
