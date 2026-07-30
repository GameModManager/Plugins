/**
 * Isaac Sort Plugin — Auto-sort + tag evaluation for The Binding of Isaac
 *
 * Separate plugin shipped as .so/.dll/.dylib. Registers a sort provider
 * for Isaac instances via the GMM ABI. Core has zero Isaac-specific code.
 */

#include "gmm_abi_v1.h"
#include "ModSorterProvider.h"

#include <cstdio>
#include <cstring>
#include <memory>

static std::unique_ptr<engine::IsaacSortProvider> g_provider;

/* ── C ABI sort function called by the engine ── */
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

/* ── Plugin entry point ── */
extern "C" uint32_t gmm_abi_version(void) {
    return GMM_ABI_VERSION;
}

extern "C" void gmm_register_v1(GmmRegistrationCtx* ctx) {
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

    // Register with the engine
    ctx->register_sort_provider(ctx, "TheBindingOfIsaacRebirth", isaac_sort, nullptr);
}
