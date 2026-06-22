#pragma once

#include <string>

namespace watcher {

struct OfficeConvertResult {
    bool success = false;
    std::string error_message;
};

// Spawns python_exe convert_script input_path output_path and waits up to
// 60 seconds. Kills the process if it hangs and returns a failure result.
// Returns success=false with an error message if python_exe or convert_script
// are empty (caller should fall back to the manual-step guide reply).
OfficeConvertResult office_convert(const std::string& python_exe,
                                    const std::string& convert_script,
                                    const std::string& input_path,
                                    const std::string& output_path);

}  // namespace watcher
