#include "ModSorterProvider.h"

namespace engine {

IsaacSortProvider::IsaacSortProvider() = default;

void IsaacSortProvider::load_masterlist(const std::string& yaml_text) {
    sorter_.load_masterlist(yaml_text);
}

void IsaacSortProvider::load_user_rules(const std::string& yaml_text) {
    sorter_.load_user_rules(yaml_text);
}

ModSortResult IsaacSortProvider::sort(const std::vector<SortModInfo>& mods) const {
    ModSortResult result;

    // Convert core SortModInfo to IsaacSortModInfo
    std::vector<IsaacSortModInfo> isaac_mods;
    for (const auto& mod : mods) {
        IsaacSortModInfo info;
        info.folder_name = mod.folder_name;
        info.display_name = mod.display_name;
        info.workshop_id = mod.workshop_id;
        info.tags = mod.tags;
        isaac_mods.push_back(info);
    }

    // Use the auto sorter to get the sorted folder order
    result.sorted_folders = sorter_.auto_sort(isaac_mods);

    // Collect all installed workshop IDs
    std::vector<int64_t> installed_ids;
    for (const auto& mod : mods) {
        if (mod.workshop_id != 0) {
            installed_ids.push_back(mod.workshop_id);
        }
    }

    // Evaluate tags for each mod
    for (const auto& mod : mods) {
        if (mod.workshop_id == 0) continue;

        auto tags = sorter_.evaluate_tags(mod.workshop_id, installed_ids);
        for (const auto& tag : tags) {
            ModSortResult::TagInfo tag_info;
            tag_info.folder_name = mod.folder_name;
            tag_info.type = tag.type;
            tag_info.message = tag.message;
            result.tags.push_back(tag_info);
        }
    }

    return result;
}

}  // namespace engine
