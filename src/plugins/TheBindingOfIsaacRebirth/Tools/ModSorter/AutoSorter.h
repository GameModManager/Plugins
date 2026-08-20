#pragma once

#include <cstdint>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace YAML { class Node; }

namespace engine {

struct SortGroup {
    std::string name;
    int priority = 99;
};

struct MasterlistTag {
    std::string type;
    std::string message;
    std::vector<int64_t> if_installed;
    std::vector<int64_t> if_not_installed;
};

struct IsaacModEntry {
    int64_t workshop_id = 0;
    std::string name;
    std::string group = "unknown";
    std::vector<int64_t> after;
    std::vector<int64_t> before;
    std::vector<int64_t> required_by;
    std::string pattern;
    std::string tag;
    bool preserve_name = false;
    std::vector<MasterlistTag> masterlist_tags;
};

struct IsaacSortModInfo {
    std::string folder_name;
    std::string display_name;
    int64_t workshop_id = 0;
    std::vector<std::string> tags;
};

class IsaacAutoSorter {
public:
    void load_masterlist(const std::string& yaml_text);
    void load_user_rules(const std::string& yaml_text);
    void set_bundled_masterlist(const std::string& yaml_text);

    std::vector<std::string> auto_sort(const std::vector<IsaacSortModInfo>& mods) const;

    [[nodiscard]] bool should_preserve_name(int64_t workshop_id) const;
    [[nodiscard]] std::string group_for(int64_t workshop_id) const;

    [[nodiscard]] std::vector<MasterlistTag> evaluate_tags(
        int64_t workshop_id,
        const std::vector<int64_t>& installed_workshop_ids) const;

    [[nodiscard]] const std::vector<SortGroup>& groups() const { return groups_; }
    [[nodiscard]] const std::vector<IsaacModEntry>& mod_entries() const { return mod_entries_; }

private:
    const IsaacModEntry* match_mod(const IsaacSortModInfo& mod) const;

    std::vector<std::string> topological_sort(
        const std::vector<IsaacSortModInfo>& group_mods,
        const std::unordered_map<int64_t, const IsaacModEntry*>& mod_lookup) const;

    static int64_t extract_workshop_id(const std::string& folder_name);

    static void parse_masterlist_yaml(const YAML::Node& root,
                                       std::vector<SortGroup>& groups,
                                       std::vector<IsaacModEntry>& mods);

    std::vector<SortGroup> groups_;
    std::vector<IsaacModEntry> mod_entries_;
    std::vector<IsaacModEntry> user_entries_;
};

}  // namespace engine
