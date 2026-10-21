# CMake lifecycle for a new game plugin

The exact block to add to the repo `CMakeLists.txt` after copying
`template/game/MyGame.cpp`. This mirrors the existing `add_game_plugin`
pattern (Skyrim SE / Isaac).

## 1. Plugin target

```cmake
add_game_plugin(<YourGame>
    src/plugins/GameSupport/<YourGame>/<YourGame>.cpp
)
```

`add_game_plugin` sets PREFIX "" and LIBRARY_OUTPUT_DIRECTORY
`build/plugins`, so the shipped artifact is `build/plugins/<YourGame>.so`,
and already puts `src/shared/BasicGame` (the BasicGame base class) on the
include path. The target name MUST equal the game_id field in your
subclass: Core defaults an instance's game_id to the .so stem.

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

- `template.instance_builds` - compiles the checked-in MyGame example
  exactly as a new author would (a `MyGame` MODULE target) and asserts the
  .so exists. Keeps the template TRUE after ABI churn. Your copy gets the
  same guarantee for free: it is built by the same `add_game_plugin`
  machinery, so an ABI break fails your build too.
- `template.hook_tags_drift` - runs `tools/check_hook_tags.sh`, which
  greps Core for every hook tag BasicGame registers and fails listing tags
  Core does not read. Run it against your copy after adding hooks via
  `extra()`:

  ```bash
  tools/check_hook_tags.sh ../Core/src   # from the repo root
  ctest -R "template\.|basic_game\." -N  # list the guard tests
  ```

- `basic_game.*` - BasicGame behavior (required-field refusal, defaults).
  Your subclass inherits the refusal guarantee; no per-game test needed
  unless you override the virtuals.

All are ctest entries, not per-build steps: the tag check runs when the
test suite runs, not on every compile.
