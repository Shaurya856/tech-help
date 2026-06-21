#include "print_job.h"

#include <windows.h>
#include <winspool.h>

namespace watcher {

namespace {

std::string last_error_message(DWORD error_code) {
    LPSTR buffer = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = (len > 0 && buffer) ? std::string(buffer, len) : "unknown error";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.pop_back();
    }
    return message;
}

PrintResult fail(const std::string& detail) {
    return PrintResult{false, detail};
}

}  // namespace

PrintResult print_pdf(const std::string& printer_name, const std::string& doc_name,
                       const std::vector<std::uint8_t>& pdf_data) {
    if (printer_name.empty()) {
        return fail("No printer is configured.");
    }

    HANDLE printer = nullptr;
    if (!OpenPrinterA(const_cast<char*>(printer_name.c_str()), &printer, nullptr)) {
        return fail("Could not reach printer \"" + printer_name + "\": " +
                     last_error_message(GetLastError()));
    }

    DOC_INFO_1A doc_info{};
    doc_info.pDocName = const_cast<char*>(doc_name.c_str());
    doc_info.pOutputFile = nullptr;
    doc_info.pDatatype = const_cast<char*>("RAW");

    DWORD job_id = StartDocPrinterA(printer, 1, reinterpret_cast<LPBYTE>(&doc_info));
    if (job_id == 0) {
        std::string error = last_error_message(GetLastError());
        ClosePrinter(printer);
        return fail("Printer \"" + printer_name + "\" rejected the print job: " + error);
    }

    if (!StartPagePrinter(printer)) {
        std::string error = last_error_message(GetLastError());
        EndDocPrinter(printer);
        ClosePrinter(printer);
        return fail("Printer \"" + printer_name + "\" rejected the print job: " + error);
    }

    DWORD total_written = 0;
    bool write_ok = true;
    while (write_ok && total_written < pdf_data.size()) {
        DWORD written = 0;
        write_ok = WritePrinter(printer, const_cast<std::uint8_t*>(pdf_data.data()) + total_written,
                                 static_cast<DWORD>(pdf_data.size()) - total_written, &written);
        if (write_ok && written == 0) {
            write_ok = false;
            break;
        }
        total_written += written;
    }

    std::string write_error;
    if (!write_ok) {
        write_error = last_error_message(GetLastError());
    }

    EndPagePrinter(printer);
    EndDocPrinter(printer);
    ClosePrinter(printer);

    if (!write_ok) {
        return fail("Sending the file to printer \"" + printer_name + "\" failed: " + write_error);
    }

    return PrintResult{true, ""};
}

}  // namespace watcher
