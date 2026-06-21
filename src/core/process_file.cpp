#include "process_file.h"

#include <algorithm>

namespace core {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string extension_of(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    return to_lower(path.substr(dot + 1));
}

}  // namespace

ProcessResult process_file(const std::string& input_path) {
    const std::string ext = extension_of(input_path);

    if (ext == "docx") {
        return {ProcessStatus::UnsupportedType, ""};
    }

    // TODO: image -> PDF conversion and PDF compression land here.
    return {ProcessStatus::UnsupportedType, ""};
}

}  // namespace core
