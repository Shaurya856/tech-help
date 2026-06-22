#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// Detects the input file type and routes it: images are converted to PDF,
// PDFs are passed through. The resulting PDF is always compressed in place.
// DOCX/XLSX and other unsupported types return ProcessStatus::UnsupportedType.
ProcessResult process_file(const std::string& input_path);

// Reorders pages of input_pdf according to page_order (1-based indices) and
// writes to output_pdf. Returns false on error (QPDF exception, bad indices).
bool reorder_pdf(const std::string& input_pdf,
                 const std::string& output_pdf,
                 const std::vector<int>& page_order);

// Combines multiple PDF paths into a single PDF at output_path, in the given
// order. Returns false if any input can't be opened.
bool combine_pdfs(const std::vector<std::string>& input_paths,
                  const std::string& output_path);

}  // namespace core
