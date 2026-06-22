#pragma once

#include <string>

namespace watcher {

// Initialise the log file. Creates %LOCALAPPDATA%\FatherTechAssist\logs\ and
// opens watcher.log for appending. Rotates at 5 MB (renames to watcher.log.1).
// Call once at startup; safe to call if LOCALAPPDATA is unset (falls back to
// the current working directory).
void log_init();

void log_info(const std::string& msg);
void log_error(const std::string& msg);

}  // namespace watcher
