#pragma once

#include "sorter/interface.h"
#include "AutoSorter.h"

#include <string>

namespace engine {
namespace Sorter {

class IsaacSortProvider : public Interface {
public:
    IsaacSortProvider();

    // Load masterlist from YAML text
    void load_masterlist(const std::string& yaml_text);

    // Load user rules from YAML text
    void load_user_rules(const std::string& yaml_text);

    // Sort mods and evaluate tags
    Result sort(const std::vector<ModInfo>& mods) const override;

    // Provider name
    const char* name() const override { return "IsaacAutoSorter"; }

private:
    IsaacAutoSorter sorter_;
};

}  // namespace Sorter
}  // namespace engine
