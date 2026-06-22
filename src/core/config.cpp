#include "config.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace core {

Config load_config(const std::string& config_path) {
    std::ifstream in(config_path);
    if (!in) {
        throw std::runtime_error("cannot open config file: " + config_path);
    }

    nlohmann::json j;
    in >> j;

    Config config;
    if (j.contains("gmail")) {
        const auto& g = j.at("gmail");
        config.gmail.address = g.value("address", "");
        config.gmail.imap_host = g.value("imap_host", "");
        config.gmail.imap_port = g.value("imap_port", 993);
        config.gmail.smtp_host = g.value("smtp_host", "");
        config.gmail.smtp_port = g.value("smtp_port", 465);
        config.gmail.app_password = g.value("app_password", "");
    }
    config.output_folder = j.value("output_folder", "");
    config.obsidian_vault_folder = j.value("obsidian_vault_folder", "");
    config.language = j.value("language", "en");
    config.printer_name = j.value("printer_name", "");
    config.max_attachment_mb = j.value("max_attachment_mb", 100);
    config.max_attachments = j.value("max_attachments", 30);
    config.python_exe = j.value("python_exe", "");
    config.office_convert_script = j.value("office_convert_script", "");

    return config;
}

}  // namespace core
