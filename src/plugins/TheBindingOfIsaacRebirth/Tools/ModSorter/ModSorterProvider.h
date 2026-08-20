#pragma once

#include "sort_provider.h"
#include "AutoSorter.h"

#include <string>

namespace engine {

class IsaacSortProvider : public SortProvider {
public:
    IsaacSortProvider();

    // Load masterlist from YAML text
    void load_masterlist(const std::string& yaml_text);

    // Load user rules from YAML text
    void load_user_rules(const std::string& yaml_text);

    // Sort mods and evaluate tags
    ModSortResult sort(const std::vector<SortModInfo>& mods) const override;

    // Provider name
    const char* name() const override { return "IsaacAutoSorter"; }

private:
    IsaacAutoSorter sorter_;
};

}  // namespace engine
