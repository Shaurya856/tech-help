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
    std::int64_t pdf_compression_threshold_mb = 5;
    std::string obsidian_vault_folder;
    std::string language = "en";
};

// Throws std::runtime_error if the file is missing or malformed.
Config load_config(const std::string& config_path);

}  // namespace core
