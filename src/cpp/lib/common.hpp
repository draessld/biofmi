#ifndef BIOFMI_COMMON_HPP
#define BIOFMI_COMMON_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace biofmi {

// Version information
constexpr const char* VERSION = "1.0.0";

// Common types
using String = std::string;
using StringSet = std::vector<String>;
using Position = uint64_t;
using Length = uint32_t;

// EDS format constants
constexpr char SET_OPEN = '{';
constexpr char SET_CLOSE = '}';
constexpr char SET_SEPARATOR = ',';
constexpr char CHANGE_SEPARATOR = '#';
constexpr char EMPTY_STRING_MARKER = '\0';

// File extensions
constexpr const char* EXT_MSA = ".msa";
constexpr const char* EXT_VCF = ".vcf";
constexpr const char* EXT_EDS = ".eds";
constexpr const char* EXT_EDZ = ".edz";
constexpr const char* EXT_LEDS = ".leds";
constexpr const char* EXT_EDP = ".edp";
constexpr const char* EXT_INDEX = ".index";

// Error codes
enum class ErrorCode {
    SUCCESS = 0,
    FILE_NOT_FOUND = 1,
    INVALID_FORMAT = 2,
    INVALID_PARAMETER = 3,
    BUILD_FAILED = 4,
    QUERY_FAILED = 5,
    UNKNOWN_ERROR = 99
};

} // namespace biofmi

#endif // BIOFMI_COMMON_HPP
