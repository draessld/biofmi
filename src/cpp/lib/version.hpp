#ifndef BIOFMI_VERSION_HPP
#define BIOFMI_VERSION_HPP

#include <string>

namespace biofmi {

// Prints the tool name and version, then COMMIT=, COMMIT_DATE= and DIRTY= for
// the BioFMI tree the binary was built from — the same lines edsparser's tools
// print, so xbench's provenance gate reads both alike.
void print_version(const std::string& tool_name);

} // namespace biofmi

#endif // BIOFMI_VERSION_HPP
