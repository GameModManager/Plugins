/**
 * MyGame - the whole base-game template.
 *
 * A new game-support plugin is this file: a small subclass of
 * gmm::BasicGame (src/shared/BasicGame/BasicGame.hpp). The base class
 * carries the entire registration - identity, plugin metadata, category,
 * default tabs, and every common hook tag - with a documented default for
 * every field. Untouched fields ride the default; unset hooks are
 * omitted, never registered with a placeholder.
 *
 * HOW TO USE
 *
 * 1. Copy this file to src/plugins/GameSupport/<YourGame>/<YourGame>.cpp.
 * 2. Rename the struct; fill the required fields; delete optional lines
 *    whose defaults already fit. Every field's default is documented
 *    inline in BasicGame.hpp next to the field.
 * 3. Wire the CMake block from template/cmake_lifecycle.md
 *    (add_game_plugin already puts BasicGame.hpp on the include path).
 * 4. Copy wine.json next to your .cpp and fill it; add your game id to
 *    the wine.json foreach in the repo CMakeLists.txt.
 *
 * REQUIRED fields (registration refuses + loud stderr error without
 * them): game_id, display_name, game_binary, mods_subpath.
 *
 * OPTIONAL overrides (see BasicGame.hpp for the full contracts):
 * - tabs()           default: Mods + Downloads (Nexus-wired when
 *                    nexus_domain resolves). Override for a different
 *                    layout (Skyrim's five-tab layout).
 * - order_encoding() default: none. Override to write your load-order
 *                    file (Skyrim's plugins.txt writer, Isaac's
 *                    metadata.xml writer).
 * - extra(Ctx&)      tools, game variants, save parsers, categories,
 *                    workshop/loot/conflict hooks - anything with no
 *                    descriptor field. Real examples:
 *                    SkyrimSpecialEdition.cpp (tool + variants + save
 *                    parser), TheBindingOfIsaacRebirth.cpp (categories +
 *                    workshop hooks). For a binary save parser, copy the
 *                    adapter pattern from SkyrimSESaveParser.cpp.
 */

#include "BasicGame.hpp"

struct MyGame : gmm::BasicGame {
  MyGame() {
    /* Required - registration refuses to run without these: */
    game_id      = "MyGame"; /* MUST equal the CMake target name */
    display_name = "My Game";
    game_binary  = "mygame.exe"; /* main exe, relative to the game dir */
    mods_subpath = "mods";       /* mod root, relative to the game dir */

    /* Optional - delete any line whose default already fits: */
    steam_appid     = 0;         /* 0 = not on Steam */
    nexus_domain    = "mygame";  /* default: lowercased game_id */
    save_extensions = "sav";     /* default: omitted */
    mygames_folder  = "My Game"; /* default: display_name */
  }
};

REGISTER_BASIC_GAME(MyGame)
