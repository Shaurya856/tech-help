#include "state.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace watcher {

std::optional<std::int64_t> load_last_uid(const std::string& state_path) {
    std::ifstream in(state_path);
    if (!in) return std::nullopt;

    nlohmann::json j;
    in >> j;
    if (!j.contains("last_uid")) return std::nullopt;
    return j.at("last_uid").get<std::int64_t>();
}

void save_last_uid(const std::string& state_path, std::int64_t uid) {
    nlohmann::json j;
    j["last_uid"] = uid;
    std::ofstream out(state_path, std::ios::trunc);
    out << j.dump(2);
}

}  // namespace watcher
