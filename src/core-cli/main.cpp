// Thin CLI wrapper around core::process_file so non-C++ callers (the Python
// MCP server) can invoke the real conversion logic via subprocess instead of
// re-implementing it.
//
// Usage: core-cli <input_path>
// Prints a single JSON object to stdout: {"status": "...", "output_path": "..."}
// status is one of "ok", "unsupported", "error".

#include <iostream>

#include "process_file.h"

namespace {

const char* status_to_string(core::ProcessStatus status) {
    switch (status) {
        case core::ProcessStatus::Ok:
            return "ok";
        case core::ProcessStatus::UnsupportedType:
            return "unsupported";
        case core::ProcessStatus::Error:
            return "error";
    }
    return "error";
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: core-cli <input_path>\n";
        return 1;
    }

    const std::string input_path = argv[1];

    core::ProcessResult result = core::process_file(input_path);

    std::cout << "{\"status\": \"" << status_to_string(result.status)
               << "\", \"output_path\": \"" << json_escape(result.output_path) << "\"}\n";

    return result.status == core::ProcessStatus::Error ? 1 : 0;
}
