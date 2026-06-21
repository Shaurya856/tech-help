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

constexpr std::int64_t kDefaultCompressionThresholdMB = 5;

// Detects the input file type and routes it: images (jpg/jpeg/png) are
// converted to PDF, PDFs are passed through and compressed in place if over
// compression_threshold_mb. DOCX and other unsupported types return
// ProcessStatus::UnsupportedType.
ProcessResult process_file(
    const std::string& input_path,
    std::int64_t compression_threshold_mb = kDefaultCompressionThresholdMB);

}  // namespace core
