#pragma once

#include <cstdint>
#include <string>

namespace core {

struct GmailConfig {
    std::string address;
    std::string imap_host;
    int imap_port = 993;
    std::string smtp_host;
    int smtp_port = 465;
    std::string app_password;
};

struct Config {
    GmailConfig gmail;
    std::string output_folder;
    std::string obsidian_vault_folder;
    std::string language = "en";
    std::string printer_name;
    std::int64_t max_attachment_mb = 100;
    int max_attachments = 30;
    // Full path to python.exe used for DOCX/XLSX COM conversion.
    // If empty, DOCX/XLSX sends the manual-step guide reply instead.
    std::string python_exe;
    // Full path to src/office/convert.py, installed alongside the executables.
    std::string office_convert_script;
};

// Throws std::runtime_error if the file is missing or malformed.
Config load_config(const std::string& config_path);

}  // namespace core
