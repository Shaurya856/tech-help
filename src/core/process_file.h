#pragma once

#include <string>

namespace core {

enum class ProcessStatus {
    Ok,
    UnsupportedType,
};

struct ProcessResult {
    ProcessStatus status;
    std::string output_path;
};

// Detects the input file type and routes it: images are converted to PDF,
// PDFs are passed through (and compressed if over the size threshold).
// DOCX and other unsupported types return ProcessStatus::UnsupportedType.
ProcessResult process_file(const std::string& input_path);

}  // namespace core
