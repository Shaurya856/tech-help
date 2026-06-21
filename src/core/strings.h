#pragma once

#include <string>
#include <unordered_map>

namespace core {

// Loads <lang_dir>/<language_code>.json, falling back to <lang_dir>/en.json
// for any key missing from the requested language (or if the requested
// language file doesn't exist at all).
class Strings {
public:
    Strings(const std::string& lang_dir, const std::string& language_code);

    const std::string& get(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> translated_;
    std::unordered_map<std::string, std::string> fallback_;
};

}  // namespace core
