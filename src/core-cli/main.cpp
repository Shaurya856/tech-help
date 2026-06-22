// Thin CLI wrapper around core so non-C++ callers (the Python MCP server)
// can invoke conversion and reordering via subprocess.
//
// Usage:
//   core-cli <input_path>
//     Convert/compress a file. Prints {"status":"...","output_path":"..."}
//
//   core-cli reorder <input_path> <output_path> <page_order>
//     Reorder PDF pages. page_order is comma-separated 1-based indices.
//     Prints {"status":"ok"} or {"status":"error","message":"..."}

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "process_file.h"

namespace {

const char* status_str(core::ProcessStatus s) {
    switch (s) {
        case core::ProcessStatus::Ok:            return "ok";
        case core::ProcessStatus::UnsupportedType: return "unsupported";
        case core::ProcessStatus::Error:         return "error";
    }
    return "error";
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

std::vector<int> parse_order(const std::string& s) {
    std::vector<int> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try { out.push_back(std::stoi(tok)); } catch (...) {}
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: core-cli <input_path>\n"
                     "       core-cli reorder <input> <output> <page_order>\n";
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "reorder") {
        if (argc < 5) {
            std::cerr << "usage: core-cli reorder <input> <output> <page_order>\n";
            return 1;
        }
        const std::string input  = argv[2];
        const std::string output = argv[3];
        std::vector<int> order = parse_order(argv[4]);
        bool ok = core::reorder_pdf(input, output, order);
        if (ok) {
            std::cout << "{\"status\":\"ok\",\"output_path\":\"" << escape(output) << "\"}\n";
        } else {
            std::cout << "{\"status\":\"error\",\"message\":\"reorder failed\"}\n";
        }
        return ok ? 0 : 1;
    }

    // Default: process_file
    const std::string input_path = cmd;
    core::ProcessResult result = core::process_file(input_path);
    std::cout << "{\"status\":\"" << status_str(result.status)
               << "\",\"output_path\":\"" << escape(result.output_path) << "\"}\n";
    return result.status == core::ProcessStatus::Error ? 1 : 0;
}
