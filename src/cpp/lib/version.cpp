#include "version.hpp"
#include "biofmi_build_info.hpp"

#include <iostream>

namespace biofmi {

void print_version(const std::string& tool_name) {
    std::cout << tool_name << " " << BIOFMI_VERSION << "\n";
    std::cout << "COMMIT=" << BIOFMI_GIT_COMMIT << "\n";
    std::cout << "COMMIT_DATE=" << BIOFMI_GIT_DATE << "\n";
    std::cout << "DIRTY=" << BIOFMI_GIT_DIRTY << "\n";
}

} // namespace biofmi
