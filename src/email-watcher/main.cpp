// Runs one check-and-reply pass, then exits. Launched on a schedule by
// Windows Task Scheduler (see install/install.ps1) rather than staying
// resident.

#include <curl/curl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

#include "config.h"
#include "imap_client.h"
#include "mime.h"
#include "print_job.h"
#include "process_file.h"
#include "smtp_client.h"
#include "state.h"
#include "strings.h"

namespace fs = std::filesystem;

namespace {

std::string exe_dir() {
    return fs::current_path().string();
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void write_file(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

bool subject_requests_print(const std::string& subject) {
    static const std::regex kPrintWord(R"(\bprint\b)", std::regex::icase);
    return std::regex_search(subject, kPrintWord);
}

std::string extension_of(const std::string& filename) {
    std::string ext = fs::path(filename).extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(c));
    return ext;
}

std::vector<mime::Attachment> load_docx_guide_images(const std::string& guide_dir) {
    std::vector<mime::Attachment> images;
    if (!fs::exists(guide_dir)) return images;

    for (const auto& entry : fs::directory_iterator(guide_dir)) {
        if (!entry.is_regular_file()) continue;
        mime::Attachment att;
        att.filename = entry.path().filename().string();
        att.content_type = "image/png";
        att.data = read_file(entry.path().string());
        images.push_back(std::move(att));
    }
    return images;
}

void reply_with_text(const watcher::SmtpConfig& smtp, const std::string& my_address,
                      const std::string& to, const std::string& subject, const std::string& body,
                      const std::vector<mime::Attachment>& attachments = {}) {
    std::string raw = mime::build_message(my_address, to, subject, body, attachments);
    watcher::send_message(smtp, my_address, to, raw);
}

}  // namespace

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const std::string base_dir = exe_dir();
    const std::string config_path = (fs::path(base_dir) / "config.json").string();
    const std::string state_path = (fs::path(base_dir) / "last_seen_state.json").string();
    const std::string guide_dir = (fs::path(base_dir) / "assets" / "docx-guide").string();
    const std::string tmp_dir = (fs::path(base_dir) / "tmp").string();

    core::Config config;
    try {
        config = core::load_config(config_path);
    } catch (const std::exception& e) {
        std::cerr << "failed to load config: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    core::Strings strings((fs::path(base_dir) / "lang").string(), config.language);

    watcher::ImapClient imap({config.gmail.imap_host, config.gmail.imap_port,
                               config.gmail.address, config.gmail.app_password});
    watcher::SmtpConfig smtp{config.gmail.smtp_host, config.gmail.smtp_port,
                              config.gmail.address, config.gmail.app_password};

    auto last_uid_opt = watcher::load_last_uid(state_path);

    try {
        std::vector<std::int64_t> uids = imap.search_uids_after(last_uid_opt.value_or(0));

        if (!last_uid_opt.has_value()) {
            // First run: establish a baseline without processing the
            // existing mailbox (avoids replying to every old email).
            std::int64_t max_uid = 0;
            for (auto uid : uids) max_uid = std::max(max_uid, uid);
            watcher::save_last_uid(state_path, max_uid);
            curl_global_cleanup();
            return 0;
        }

        std::int64_t highest_processed = last_uid_opt.value();
        fs::create_directories(tmp_dir);

        for (std::int64_t uid : uids) {
            std::string raw_message = imap.fetch_message(uid);
            mime::ParsedMessage parsed = mime::parse_message(raw_message);
            highest_processed = std::max(highest_processed, uid);

            if (parsed.attachments.empty()) continue;

            const bool wants_print = subject_requests_print(parsed.subject);

            for (const auto& attachment : parsed.attachments) {
                std::string ext = extension_of(attachment.filename);
                std::string tmp_path = (fs::path(tmp_dir) / attachment.filename).string();
                write_file(tmp_path, attachment.data);

                if (ext == ".docx") {
                    reply_with_text(smtp, config.gmail.address, parsed.from_address,
                                     strings.get("email.reply_docx_subject"),
                                     strings.get("email.reply_docx_body"),
                                     load_docx_guide_images(guide_dir));
                    continue;
                }

                core::ProcessResult result = core::process_file(tmp_path);

                if (result.status == core::ProcessStatus::Ok) {
                    mime::Attachment reply_attachment;
                    reply_attachment.filename = fs::path(result.output_path).filename().string();
                    reply_attachment.content_type = "application/pdf";
                    reply_attachment.data = read_file(result.output_path);

                    if (wants_print) {
                        watcher::PrintResult print_result = watcher::print_pdf(
                            config.printer_name, reply_attachment.filename, reply_attachment.data);

                        if (print_result.success) {
                            reply_with_text(smtp, config.gmail.address, parsed.from_address,
                                             strings.get("email.reply_printed_subject"),
                                             strings.get("email.reply_printed_body"),
                                             {reply_attachment});
                        } else {
                            std::string body = strings.get("email.reply_print_failed_body") +
                                                "\n\nDetails: " + print_result.error_message;
                            reply_with_text(smtp, config.gmail.address, parsed.from_address,
                                             strings.get("email.reply_print_failed_subject"), body,
                                             {reply_attachment});
                        }
                        continue;
                    }

                    reply_with_text(smtp, config.gmail.address, parsed.from_address,
                                     strings.get("email.reply_success_subject"),
                                     strings.get("email.reply_success_body"), {reply_attachment});
                } else if (result.status == core::ProcessStatus::UnsupportedType) {
                    reply_with_text(smtp, config.gmail.address, parsed.from_address,
                                     strings.get("email.reply_unsupported_subject"),
                                     strings.get("email.reply_unsupported_body"));
                } else {
                    reply_with_text(smtp, config.gmail.address, parsed.from_address,
                                     strings.get("email.reply_error_subject"),
                                     strings.get("email.reply_error_body"));
                }
            }
        }

        watcher::save_last_uid(state_path, highest_processed);
    } catch (const std::exception& e) {
        std::cerr << "email-watcher run failed: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return 0;
}
