#include "strings.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace core {

namespace {

std::unordered_map<std::string, std::string> load_map(const std::string& path) {
    std::unordered_map<std::string, std::string> map;
    std::ifstream in(path);
    if (!in) return map;

    nlohmann::json j;
    in >> j;
    for (auto& [key, value] : j.items()) {
        if (value.is_string()) map[key] = value.get<std::string>();
    }
    return map;
}

}  // namespace

Strings::Strings(const std::string& lang_dir, const std::string& language_code) {
    fallback_ = load_map((std::filesystem::path(lang_dir) / "en.json").string());
    if (language_code != "en") {
        translated_ = load_map((std::filesystem::path(lang_dir) / (language_code + ".json")).string());
    }
}

const std::string& Strings::get(const std::string& key) const {
    auto it = translated_.find(key);
    if (it != translated_.end()) return it->second;

    it = fallback_.find(key);
    if (it != fallback_.end()) return it->second;

    static const std::string missing;
    return missing;
}

}  // namespace core
