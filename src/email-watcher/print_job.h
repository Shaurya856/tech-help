#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace watcher {

struct PrintResult {
    bool success = false;
    std::string error_message;  // Set when success is false.
};

// Submits pdf_data directly to printer_name via the Windows print spooler
// (winspool.h), with no intermediate viewer/application. Requires a printer
// or driver that accepts raw PDF data passed straight through to the device.
PrintResult print_pdf(const std::string& printer_name, const std::string& doc_name,
                       const std::vector<std::uint8_t>& pdf_data);

}  // namespace watcher
