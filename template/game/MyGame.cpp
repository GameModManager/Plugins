/**
 * MyGame - the whole base-game template (v3 SDK shape).
 *
 * A new game-support plugin is this file: a small subclass of
 * gmm::BasicGame (gmm/basic_game.h, header-only SDK). The base class
 * carries the entire registration - identity, plugin metadata, category,
 * default tabs, and every common hook tag - with a documented default for
 * every field. Untouched fields ride the default; unset hooks are
 * omitted, never registered with a placeholder.
 *
 * HOW TO USE
 *
 * 1. Copy this file to src/plugins/GameSupport/<YourGame>/<YourGame>.cpp.
 * 2. Rename the struct; fill the required fields (id, name, mods_dir);
 *    delete optional lines whose defaults already fit. Every field's
 *    default is documented inline in gmm/basic_game.h next to the field.
 * 3. Wire the CMake block from template/cmake_lifecycle.md
 *    (add_game_plugin already puts the SDK on the include path).
 * 4. Copy wine.json next to your .cpp and fill it; add your game id to
 *    the wine.json foreach in the repo CMakeLists.txt.
 *
 * REQUIRED fields (compile error without them): id, name, mods_dir.
 *
 * OPTIONAL overrides:
 * - attach(World&)  default: none. Override to attach capability classes
 *                   (save parsers, previews, tools, ...).
 */

#include "gmm/basic_game.h"

#include <vector>

struct MyGame : gmm::BasicGame {
  /* Required - registration refuses to run without these: */
  const char *id                     = "MyGame"; /* MUST equal the CMake target name */
  const char *name                   = "My Game";
  gmm::ByTarget<const char *> binary = {.windows = "mygame.exe"}; /* main exe */
  const char *mods_dir               = "mods"; /* mod root, relative to game dir */

  /* Optional - delete any line whose default already fits: */
  std::vector<uint32_t> steam_appids = {};       /* empty = not on Steam */
  const char *nexus_domain           = "mygame"; /* default: lowercased id */
  gmm::Saves saves                   = {.extensions = {"sav"}}; /* default: omitted */
};

GMM_BASIC_GAME(MyGame)
