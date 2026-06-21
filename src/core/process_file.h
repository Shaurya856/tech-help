#pragma once

#include <cstdint>
#include <string>

namespace core {

enum class ProcessStatus {
    Ok,
    UnsupportedType,
    Error,
};

struct ProcessResult {
    ProcessStatus status;
    std::string output_path;
};

// Detects the input file type and routes it: images (jpg/jpeg/png) are
// converted to PDF, PDFs are passed through. The resulting PDF is always
// compressed in place. DOCX and other unsupported types return
// ProcessStatus::UnsupportedType.
ProcessResult process_file(const std::string& input_path);

}  // namespace core
