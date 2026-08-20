#include "AutoSorter.h"

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

namespace engine {

// -- Workshop ID extraction --

int64_t IsaacAutoSorter::extract_workshop_id(const std::string& folder_name) {
    static const std::regex re(R"(_(\d+)$)");
    std::smatch m;
    if (std::regex_search(folder_name, m, re)) {
        try { return std::stoll(m[1].str()); } catch (...) {}
    }
    return 0;
}

// -- YAML parsing --

static std::vector<int64_t> yaml_to_int_list(const YAML::Node& node) {
    std::vector<int64_t> result;
    if (!node || !node.IsSequence()) return result;
    for (const auto& item : node) {
        try { result.push_back(item.as<int64_t>()); } catch (...) {}
    }
    return result;
}

void IsaacAutoSorter::parse_masterlist_yaml(
        const YAML::Node& root,
        std::vector<SortGroup>& groups,
        std::vector<IsaacModEntry>& mods) {
    groups.clear();
    mods.clear();

    if (root["groups"] && root["groups"].IsSequence()) {
        for (const auto& g : root["groups"]) {
            SortGroup sg;
            if (g["name"]) sg.name = g["name"].as<std::string>();
            if (g["priority"]) sg.priority = g["priority"].as<int>();
            groups.push_back(sg);
        }
    }

    if (root["mods"] && root["mods"].IsSequence()) {
        for (const auto& m : root["mods"]) {
            IsaacModEntry me;
            if (m["id"]) me.workshop_id = m["id"].as<int64_t>();
            if (m["name"]) me.name = m["name"].as<std::string>();
            if (m["group"]) me.group = m["group"].as<std::string>();
            if (m["after"]) me.after = yaml_to_int_list(m["after"]);
            if (m["before"]) me.before = yaml_to_int_list(m["before"]);
            if (m["requires"]) me.required_by = yaml_to_int_list(m["requires"]);
            if (m["pattern"]) me.pattern = m["pattern"].as<std::string>();
            if (m["tag"]) me.tag = m["tag"].as<std::string>();
            if (m["preserve_name"]) me.preserve_name = m["preserve_name"].as<bool>();

            // Parse tags
            if (m["tags"] && m["tags"].IsSequence()) {
                for (const auto& t : m["tags"]) {
                    MasterlistTag mt;
                    if (t["type"]) mt.type = t["type"].as<std::string>();
                    if (t["message"]) mt.message = t["message"].as<std::string>();
                    if (t["condition"]) {
                        const auto& cond = t["condition"];
                        if (cond["if_installed"]) mt.if_installed = yaml_to_int_list(cond["if_installed"]);
                        if (cond["if_not_installed"]) mt.if_not_installed = yaml_to_int_list(cond["if_not_installed"]);
                    }
                    me.masterlist_tags.push_back(mt);
                }
            }

            mods.push_back(me);
        }
    }
}

// -- Public interface --

void IsaacAutoSorter::load_masterlist(const std::string& yaml_text) {
    YAML::Node root = YAML::Load(yaml_text);
    parse_masterlist_yaml(root, groups_, mod_entries_);
}

void IsaacAutoSorter::load_user_rules(const std::string& yaml_text) {
    user_entries_.clear();
    std::vector<SortGroup> dummy;
    YAML::Node root = YAML::Load(yaml_text);
    parse_masterlist_yaml(root, dummy, user_entries_);
}

void IsaacAutoSorter::set_bundled_masterlist(const std::string& yaml_text) {
    if (mod_entries_.empty()) {
        YAML::Node root = YAML::Load(yaml_text);
        parse_masterlist_yaml(root, groups_, mod_entries_);
    }
}

bool IsaacAutoSorter::should_preserve_name(int64_t workshop_id) const {
    for (const auto& e : mod_entries_)
        if (e.workshop_id == workshop_id && e.preserve_name) return true;
    for (const auto& e : user_entries_)
        if (e.workshop_id == workshop_id && e.preserve_name) return true;
    return false;
}

std::string IsaacAutoSorter::group_for(int64_t workshop_id) const {
    for (const auto& e : mod_entries_)
        if (e.workshop_id == workshop_id) return e.group;
    for (const auto& e : user_entries_)
        if (e.workshop_id == workshop_id) return e.group;
    return "unknown";
}

std::vector<MasterlistTag> IsaacAutoSorter::evaluate_tags(
        int64_t workshop_id,
        const std::vector<int64_t>& installed_workshop_ids) const {
    // Find the mod entry
    const IsaacModEntry* entry = nullptr;
    for (const auto& e : user_entries_)
        if (e.workshop_id == workshop_id) { entry = &e; break; }
    if (!entry) {
        for (const auto& e : mod_entries_)
            if (e.workshop_id == workshop_id) { entry = &e; break; }
    }
    if (!entry) return {};

    // Build a set of installed IDs for fast lookup
    std::unordered_set<int64_t> installed_set(
        installed_workshop_ids.begin(), installed_workshop_ids.end());

    // Evaluate each tag
    std::vector<MasterlistTag> result;
    for (const auto& tag : entry->masterlist_tags) {
        bool applies = true;

        // Check if_installed: tag applies if ANY of these are installed
        if (!tag.if_installed.empty()) {
            bool any_installed = false;
            for (int64_t id : tag.if_installed) {
                if (installed_set.count(id)) {
                    any_installed = true;
                    break;
                }
            }
            if (!any_installed) applies = false;
        }

        // Check if_not_installed: tag applies if NONE of these are installed
        if (applies && !tag.if_not_installed.empty()) {
            for (int64_t id : tag.if_not_installed) {
                if (installed_set.count(id)) {
                    applies = false;
                    break;
                }
            }
        }

        if (applies) {
            result.push_back(tag);
        }
    }

    return result;
}

// -- Mod matching --

const IsaacModEntry* IsaacAutoSorter::match_mod(const IsaacSortModInfo& mod) const {
    // 1. Workshop ID match (user entries override)
    if (mod.workshop_id != 0) {
        for (const auto& e : user_entries_)
            if (e.workshop_id == mod.workshop_id) return &e;
        for (const auto& e : mod_entries_)
            if (e.workshop_id == mod.workshop_id) return &e;
    }

    // 2. Pattern match (regex against folder name)
    for (const auto& e : user_entries_) {
        if (!e.pattern.empty()) {
            try {
                if (std::regex_search(mod.folder_name,
                        std::regex(e.pattern, std::regex::icase)))
                    return &e;
            } catch (...) {}
        }
    }
    for (const auto& e : mod_entries_) {
        if (!e.pattern.empty()) {
            try {
                if (std::regex_search(mod.folder_name,
                        std::regex(e.pattern, std::regex::icase)))
                    return &e;
            } catch (...) {}
        }
    }

    // 3. Tag match
    for (const auto& e : user_entries_) {
        if (!e.tag.empty())
            for (const auto& t : mod.tags)
                if (t == e.tag) return &e;
    }
    for (const auto& e : mod_entries_) {
        if (!e.tag.empty())
            for (const auto& t : mod.tags)
                if (t == e.tag) return &e;
    }

    return nullptr;
}

// -- Topological sort --

std::vector<std::string> IsaacAutoSorter::topological_sort(
        const std::vector<IsaacSortModInfo>& group_mods,
        const std::unordered_map<int64_t, const IsaacModEntry*>& mod_lookup) const {
    if (group_mods.empty()) return {};

    struct SortItem { std::string key; std::string folder_name; };
    std::vector<SortItem> items;
    std::unordered_map<std::string, size_t> key_to_idx;

    for (const auto& mod : group_mods) {
        SortItem item;
        item.key = (mod.workshop_id != 0)
            ? "id:" + std::to_string(mod.workshop_id)
            : "folder:" + mod.folder_name;
        item.folder_name = mod.folder_name;
        key_to_idx[item.key] = items.size();
        items.push_back(item);
    }

    std::unordered_map<std::string, std::vector<std::string>> graph;
    std::unordered_map<std::string, int> in_degree;
    for (const auto& item : items) in_degree[item.key] = 0;

    for (const auto& mod : group_mods) {
        std::string mod_key = (mod.workshop_id != 0)
            ? "id:" + std::to_string(mod.workshop_id)
            : "folder:" + mod.folder_name;

        const IsaacModEntry* entry = nullptr;
        if (mod.workshop_id != 0) {
            auto it = mod_lookup.find(mod.workshop_id);
            if (it != mod_lookup.end()) entry = it->second;
        }
        if (!entry) continue;

        for (int64_t dep : entry->after) {
            std::string dk = "id:" + std::to_string(dep);
            if (key_to_idx.count(dk)) { graph[dk].push_back(mod_key); in_degree[mod_key]++; }
        }
        for (int64_t dep : entry->before) {
            std::string dk = "id:" + std::to_string(dep);
            if (key_to_idx.count(dk)) { graph[mod_key].push_back(dk); in_degree[dk]++; }
        }
    }

    // Kahn's algorithm
    std::deque<std::string> queue;
    for (const auto& [k, d] : in_degree)
        if (d == 0) queue.push_back(k);

    std::vector<std::string> result;
    while (!queue.empty()) {
        std::string cur = queue.front(); queue.pop_front();
        result.push_back(cur);
        if (graph.count(cur))
            for (const auto& dep : graph[cur])
                if (--in_degree[dep] == 0) queue.push_back(dep);
    }

    // Reconnect any leftover (cycles)
    for (const auto& item : items)
        if (std::find(result.begin(), result.end(), item.key) == result.end())
            result.push_back(item.key);

    // Convert keys to folder names
    std::unordered_map<std::string, std::string> key_to_folder;
    for (const auto& item : items) key_to_folder[item.key] = item.folder_name;

    std::vector<std::string> out;
    for (const auto& k : result) {
        auto it = key_to_folder.find(k);
        if (it != key_to_folder.end()) out.push_back(it->second);
    }
    return out;
}

// -- Main sort --

std::vector<std::string> IsaacAutoSorter::auto_sort(
        const std::vector<IsaacSortModInfo>& mods) const {
    // 1. Classify mods into groups
    std::unordered_map<std::string, std::vector<IsaacSortModInfo>> groups_map;
    for (const auto& mod : mods) {
        const IsaacModEntry* entry = match_mod(mod);
        groups_map[entry ? entry->group : "unknown"].push_back(mod);
    }

    // 2. Sort groups by priority
    std::unordered_map<std::string, int> group_prio;
    for (const auto& g : groups_) group_prio[g.name] = g.priority;

    std::vector<std::pair<std::string, std::vector<IsaacSortModInfo>>> sorted_groups(
        groups_map.begin(), groups_map.end());
    std::sort(sorted_groups.begin(), sorted_groups.end(),
        [&group_prio](const auto& a, const auto& b) {
            return (group_prio.count(a.first) ? group_prio[a.first] : 99)
                 < (group_prio.count(b.first) ? group_prio[b.first] : 99);
        });

    // 3. Build lookup
    std::unordered_map<int64_t, const IsaacModEntry*> lookup;
    for (const auto& e : mod_entries_)
        if (e.workshop_id != 0) lookup[e.workshop_id] = &e;
    for (const auto& e : user_entries_)
        if (e.workshop_id != 0) lookup[e.workshop_id] = &e;

    // 4. Topological sort within each group, concatenate
    std::vector<std::string> result;
    for (auto& [name, group_mods] : sorted_groups) {
        auto sorted = topological_sort(group_mods, lookup);
        result.insert(result.end(), sorted.begin(), sorted.end());
    }
    return result;
}

}  // namespace engine
