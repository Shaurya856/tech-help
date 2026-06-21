#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace watcher {

// last_uid is unset only on the very first run (no state file yet).
std::optional<std::int64_t> load_last_uid(const std::string& state_path);
void save_last_uid(const std::string& state_path, std::int64_t uid);

}  // namespace watcher
