/**
 * Isaac Sort Plugin — Auto-sort + tag evaluation for The Binding of Isaac
 *
 * Separate plugin shipped as .so/.dll/.dylib. Registers a sort provider
 * for Isaac instances via the GMM ABI. Core has zero Isaac-specific code.
 */

#include "gmm_abi_v2.h"
#include "ModSorterProvider.h"

#include <cstdio>
#include <cstring>
#include <memory>

static std::unique_ptr<engine::IsaacSortProvider> g_provider;

/* -- C ABI sort function called by the engine -- */
static const char* const* isaac_sort(const char* const* mod_folders,
                                      size_t count,
                                      void* user_data) {
    if (!g_provider) return nullptr;

    // Build SortModInfo list from folder names
    std::vector<engine::SortModInfo> mods;
    for (size_t i = 0; i < count; ++i) {
        engine::SortModInfo info;
        info.folder_name = mod_folders[i] ? mod_folders[i] : "";
        info.display_name = info.folder_name;  // Will be replaced by sorter

        // Extract workshop ID from folder name
        static const std::regex re(R"(_(\d+)$)");
        std::smatch m;
        if (std::regex_search(info.folder_name, m, re)) {
            try { info.workshop_id = std::stoll(m[1].str()); } catch (...) {}
        }

        mods.push_back(info);
    }

    // Sort
    auto result = g_provider->sort(mods);

    // Build output array (null-terminated)
    static thread_local std::vector<const char*> output;
    output.clear();
    for (const auto& folder : result.sorted_folders) {
        output.push_back(folder.c_str());
    }
    output.push_back(nullptr);

    return output.data();
}

/* -- Plugin entry point -- */
extern "C" uint32_t gmm_abi_version(void) {
    return GMM_ABI_VERSION;
}

extern "C" void gmm_register_v2(GmmRegistrationCtxV2* ctx) {
    if (!ctx) return;

    // Create the provider
    g_provider = std::make_unique<engine::IsaacSortProvider>();

    // Try to load bundled masterlist (embedded as string literal for now)
    // In production, this would be loaded from a file or URL
    static const char* bundled_masterlist =
        "groups:\n"
        "  - name: framework\n"
        "    priority: 0\n"
        "  - name: libraries\n"
        "    priority: 10\n"
        "  - name: content\n"
        "    priority: 20\n"
        "  - name: tweaks\n"
        "    priority: 30\n"
        "  - name: graphics\n"
        "    priority: 40\n"
        "  - name: music\n"
        "    priority: 45\n"
        "  - name: unknown\n"
        "    priority: 99\n";

    g_provider->load_masterlist(bundled_masterlist);

    // Register the game this sort provider belongs to. v2's register_sort_provider
    // takes no game_id (unlike v1), so the engine scopes the provider to this
    // plugin's own game_id — which we set here. This mirrors the v1 call that
    // passed "TheBindingOfIsaacRebirth" explicitly to register_sort_provider.
    GmmGameInfo game{};
    game.game_id = "TheBindingOfIsaacRebirth";
    game.display_name = "The Binding of Isaac: Rebirth";
    game.steam_appid = 250900;
    game.nexus_domain = "thebindingofisaacrebirth";
    ctx->register_game(ctx, game);

    // Register the sort provider with the engine (scoped to the game above).
    ctx->register_sort_provider(ctx, isaac_sort, nullptr);

    // Plugin metadata
    GmmPluginInfo info{};
    info.name = "Isaac Mod Sorter";
    info.author = "GameModManager Team";
    info.version = VERSION;
    info.description =
        "LOOT-style mod sorter for The Binding of Isaac: Rebirth";
    ctx->register_plugin(ctx, info);

    // User-facing options (key:value pairs, rendered as editable rows in the
    // Plugins settings tab).
    if (ctx->register_settings) {
        static const char* settings_keys[] = {
            "masterlist_url",    // remote masterlist to fetch
            "auto_sort_on_load", // "1" = run the sort when mods load
        };
        static const char* settings_values[] = {
            "https://raw.githubusercontent.com/GameModManager/Masterlist/"
            "refs/heads/main/games/thebindingofisaacrebirth/masterlist.yaml",
            "1",
        };
        ctx->register_settings(ctx, settings_keys, settings_values,
                               sizeof(settings_keys) / sizeof(settings_keys[0]));
    }
}
