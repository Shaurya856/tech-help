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
    config.pdf_compression_threshold_mb =
        j.value("pdf_compression_threshold_mb", static_cast<std::int64_t>(5));
    config.obsidian_vault_folder = j.value("obsidian_vault_folder", "");
    config.language = j.value("language", "en");

    return config;
}

}  // namespace core
