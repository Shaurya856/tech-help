// Runs one check-and-reply pass, then exits. Launched on a schedule by
// Windows Task Scheduler (see install/install.ps1) rather than staying
// resident.

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "config.h"
#include "imap_client.h"
#include "image_compress.h"
#include "log.h"
#include "mime.h"
#include "office_convert.h"
#include "print_job.h"
#include "process_file.h"
#include "smtp_client.h"
#include "state.h"
#include "strings.h"

namespace fs = std::filesystem;

namespace {

// ── Helpers ────────────────────────────────────────────────────────────────

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

std::string extension_of(const std::string& filename) {
    std::string ext = fs::path(filename).extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(c));
    return ext;
}

std::int64_t unix_now() {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// Microsecond-resolution ID — unique enough for our single-threaded watcher.
std::string generate_id(const std::string& prefix) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix + std::to_string(us);
}

std::string generate_message_id() {
    return "<" + generate_id("") + ".tech-help@local>";
}

// ── Subject parsing ────────────────────────────────────────────────────────

bool subject_requests_print(const std::string& subject) {
    static const std::regex kPrintWord(R"(\bprint\b)", std::regex::icase);
    return std::regex_search(subject, kPrintWord);
}

// If subject contains "name: <text>", return the sanitized text.
// Returns empty string if the pattern isn't present.
std::string extract_name_from_subject(const std::string& subject) {
    static const std::regex kName(R"(name:\s*(.+))", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(subject, m, kName)) return "";

    std::string raw = m[1].str();
    // Sanitize: keep alphanumerics, spaces, dots, hyphens, underscores.
    std::string safe;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == ' ' || c == '.' || c == '-' || c == '_') {
            safe += c;
        } else {
            safe += '_';
        }
    }
    // Trim trailing dots/spaces (Windows filename rules).
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.')) {
        safe.pop_back();
    }
    return safe;
}

// Build output filename: use name: subject tag if present, else
// original stem + timestamp.
std::string make_output_stem(const std::string& subject,
                              const std::string& original_filename) {
    std::string named = extract_name_from_subject(subject);
    if (!named.empty()) return named;

    std::string stem = fs::path(original_filename).stem().string();
    // Append a short timestamp so repeated sends don't overwrite.
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return stem + "_" + std::to_string(sec);
}

// ── YES detection ──────────────────────────────────────────────────────────

bool body_says_yes(const std::string& body_text) {
    // Trim leading whitespace and check if the reply starts with "yes"
    // (case-insensitive). Email quote blocks follow later, so we only
    // look at the start of the actual reply text.
    size_t start = body_text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || body_text.size() - start < 3) return false;
    std::string prefix = body_text.substr(start, 3);
    for (auto& c : prefix) c = static_cast<char>(std::tolower(c));
    return prefix == "yes";
}

// ── DOCX guide images ──────────────────────────────────────────────────────

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

// ── SMTP helpers ───────────────────────────────────────────────────────────

void do_reply(const watcher::SmtpConfig& smtp,
              const std::string& my_address,
              const std::string& to,
              const std::string& subject,
              const std::string& body,
              const std::vector<mime::Attachment>& attachments = {},
              const std::string& message_id = "",
              const std::string& in_reply_to = "") {
    std::string raw = mime::build_message(
        my_address, to, subject, body, attachments, message_id, in_reply_to);
    watcher::send_message(smtp, my_address, to, raw);
}

// ── Output folder ──────────────────────────────────────────────────────────

std::string move_to_output_folder(const std::string& path,
                                   const std::string& output_folder) {
    if (output_folder.empty()) return path;
    std::error_code ec;
    fs::create_directories(output_folder, ec);
    if (ec) return path;
    fs::path dest = fs::path(output_folder) / fs::path(path).filename();
    fs::rename(path, dest, ec);
    return ec ? path : dest.string();
}

// ── Image extension check (mirrors process_file.cpp) ──────────────────────

// ── New-message processing ─────────────────────────────────────────────────
//
// Multiple attachments in one email are combined into a single ordered PDF —
// the same behavior as picking multiple files in the desktop UI. A single
// attachment keeps its specific failure replies (e.g. the DOCX manual guide);
// with multiple attachments, a file that fails to convert is silently
// dropped from the combine (matching the UI's combine_selected_files), and
// the email only fails outright if nothing could be converted at all.
void process_new_message(watcher::WatcherDB& db,
                          const watcher::SmtpConfig& smtp,
                          core::Strings& strings,
                          core::Config& config,
                          const mime::ParsedMessage& parsed,
                          const std::string& tmp_dir,
                          const std::string& pending_dir,
                          const std::string& guide_dir,
                          std::int64_t now) {
    const bool wants_print = subject_requests_print(parsed.subject);
    const bool single_attachment = parsed.attachments.size() == 1;

    auto finish_with_pdf = [&](const std::string& pdf_path,
                                const std::vector<std::uint8_t>* preview_source_image) {
        if (wants_print) {
            const std::string pending_pdf =
                (fs::path(pending_dir) / fs::path(pdf_path).filename()).string();
            fs::copy_file(pdf_path, pending_pdf, fs::copy_options::overwrite_existing);
            fs::remove(pdf_path);

            mime::Attachment preview;
            if (preview_source_image) {
                try {
                    core::RecompressedImage thumb = core::recompress_to_jpeg(
                        *preview_source_image, /*max_dimension=*/800, /*quality=*/75);
                    preview.filename = fs::path(pending_pdf).stem().string() + "_preview.jpg";
                    preview.content_type = "image/jpeg";
                    preview.data = thumb.jpeg_data;
                } catch (...) {
                    preview.filename = fs::path(pending_pdf).filename().string();
                    preview.content_type = "application/pdf";
                    preview.data = read_file(pending_pdf);
                }
            } else {
                preview.filename = fs::path(pending_pdf).filename().string();
                preview.content_type = "application/pdf";
                preview.data = read_file(pending_pdf);
            }

            const std::string confirm_msg_id = generate_message_id();
            const std::string confirm_subject = strings.get("email.reply_print_confirm_subject");

            do_reply(smtp, config.gmail.address, parsed.from_address,
                     confirm_subject, strings.get("email.reply_print_confirm_body"),
                     {preview}, confirm_msg_id, parsed.message_id);

            watcher::PendingPrintJob job;
            job.job_id             = generate_id("job-");
            job.confirm_message_id = confirm_msg_id;
            job.from_address       = parsed.from_address;
            job.confirm_subject    = confirm_subject;
            job.pdf_path           = pending_pdf;
            job.created_at         = now;
            db.save_print_job(job);
        } else {
            const std::string saved = move_to_output_folder(pdf_path, config.output_folder);
            mime::Attachment reply_att;
            reply_att.filename     = fs::path(saved).filename().string();
            reply_att.content_type = "application/pdf";
            reply_att.data         = read_file(saved);
            do_reply(smtp, config.gmail.address, parsed.from_address,
                     strings.get("email.reply_success_subject"),
                     strings.get("email.reply_success_body"), {reply_att});
        }
    };

    std::vector<std::string> temp_pdfs;

    for (const auto& attachment : parsed.attachments) {
        const std::string ext = extension_of(attachment.filename);
        const std::string tmp_in = (fs::path(tmp_dir) / attachment.filename).string();
        write_file(tmp_in, attachment.data);

        if (core::is_office_ext(ext)) {
            if (config.python_exe.empty()) {
                if (single_attachment) {
                    do_reply(smtp, config.gmail.address, parsed.from_address,
                             strings.get("email.reply_docx_subject"),
                             strings.get("email.reply_docx_body"),
                             load_docx_guide_images(guide_dir));
                    return;
                }
                continue;  // multi-attachment: drop this one from the combine
            }

            const std::string office_out = (fs::path(tmp_dir) /
                (fs::path(attachment.filename).stem().string() + "_" + generate_id("office") + ".pdf")).string();
            core::OfficeConvertResult office_result = core::office_convert(
                config.python_exe, config.office_convert_script, tmp_in, office_out);

            if (!office_result.success) {
                watcher::log_error("office_convert failed: " + office_result.error_message);
                if (single_attachment) {
                    do_reply(smtp, config.gmail.address, parsed.from_address,
                             strings.get("email.reply_error_subject"),
                             strings.get("email.reply_error_body"));
                    return;
                }
                continue;
            }

            core::ProcessResult compress_result = core::process_file(office_out);
            temp_pdfs.push_back(compress_result.status == core::ProcessStatus::Ok
                                     ? compress_result.output_path : office_out);
            continue;
        }

        core::ProcessResult result = core::process_file(tmp_in);
        if (result.status == core::ProcessStatus::Ok) {
            temp_pdfs.push_back(result.output_path);
        } else if (single_attachment) {
            if (result.status == core::ProcessStatus::UnsupportedType) {
                do_reply(smtp, config.gmail.address, parsed.from_address,
                         strings.get("email.reply_unsupported_subject"),
                         strings.get("email.reply_unsupported_body"));
            } else {
                do_reply(smtp, config.gmail.address, parsed.from_address,
                         strings.get("email.reply_error_subject"),
                         strings.get("email.reply_error_body"));
            }
            return;
        }
        // multi-attachment + failed: silently dropped from the combine.
    }

    if (temp_pdfs.empty()) {
        do_reply(smtp, config.gmail.address, parsed.from_address,
                 strings.get("email.reply_error_subject"),
                 strings.get("email.reply_error_body"));
        return;
    }

    const std::string out_stem = make_output_stem(parsed.subject, parsed.attachments[0].filename);
    std::string final_pdf;

    if (temp_pdfs.size() == 1) {
        final_pdf = temp_pdfs[0];
        const std::string renamed = (fs::path(tmp_dir) / (out_stem + ".pdf")).string();
        std::error_code ec;
        fs::rename(final_pdf, renamed, ec);
        if (!ec) final_pdf = renamed;
    } else {
        final_pdf = (fs::path(tmp_dir) / (out_stem + ".pdf")).string();
        if (!core::combine_pdfs(temp_pdfs, final_pdf)) {
            do_reply(smtp, config.gmail.address, parsed.from_address,
                     strings.get("email.reply_error_subject"),
                     strings.get("email.reply_error_body"));
            return;
        }
        for (const auto& p : temp_pdfs) {
            std::error_code ec;
            fs::remove(p, ec);
        }
    }

    const std::vector<std::uint8_t>* preview_ptr = nullptr;
    if (single_attachment && core::is_image_ext(extension_of(parsed.attachments[0].filename))) {
        preview_ptr = &parsed.attachments[0].data;
    }

    finish_with_pdf(final_pdf, preview_ptr);
}

}  // namespace

// ── main ───────────────────────────────────────────────────────────────────

int main() {
    watcher::log_init();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const std::string base_dir   = exe_dir();
    const std::string config_path = (fs::path(base_dir) / "config.json").string();
    const std::string db_path     = (fs::path(base_dir) / "watcher.db").string();
    const std::string guide_dir   = (fs::path(base_dir) / "assets" / "docx-guide").string();
    const std::string tmp_dir     = (fs::path(base_dir) / "tmp").string();
    const std::string pending_dir = (fs::path(base_dir) / "pending").string();

    core::Config config;
    try {
        config = core::load_config(config_path);
    } catch (const std::exception& e) {
        watcher::log_error(std::string("failed to load config: ") + e.what());
        std::cerr << "failed to load config: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    core::Strings strings((fs::path(base_dir) / "lang").string(), config.language);

    watcher::WatcherDB db(db_path);

    watcher::ImapClient imap({config.gmail.imap_host, config.gmail.imap_port,
                               config.gmail.address, config.gmail.app_password});
    watcher::SmtpConfig smtp{config.gmail.smtp_host, config.gmail.smtp_port,
                              config.gmail.address, config.gmail.app_password};

    try {
        fs::create_directories(tmp_dir);
        fs::create_directories(pending_dir);

        // ── Step 1: Expire pending print jobs older than 24 hours ──────────
        const std::int64_t now = unix_now();
        constexpr std::int64_t kTimeoutSeconds = 24 * 3600;
        for (const auto& job : db.get_expired_jobs(now - kTimeoutSeconds)) {
            try {
                do_reply(smtp, config.gmail.address, job.from_address,
                         strings.get("email.reply_print_expired_subject"),
                         strings.get("email.reply_print_expired_body"));
            } catch (...) {}
            fs::remove(job.pdf_path);
            db.delete_print_job(job.job_id);
        }

        // ── Step 2: Fetch new messages since last run ──────────────────────
        auto last_uid_opt = db.load_last_uid();
        std::vector<std::int64_t> uids = imap.search_uids_after(last_uid_opt.value_or(0));

        if (!last_uid_opt.has_value()) {
            // First run: establish a baseline — don't process the existing
            // mailbox, just record the current high-water mark.
            std::int64_t max_uid = 0;
            for (auto uid : uids) max_uid = std::max(max_uid, uid);
            db.save_last_uid(max_uid);
            curl_global_cleanup();
            return 0;
        }

        std::int64_t highest_processed = last_uid_opt.value();

        for (std::int64_t uid : uids) {
            highest_processed = std::max(highest_processed, uid);

            std::string raw_message = imap.fetch_message(uid);
            mime::ParsedMessage parsed = mime::parse_message(raw_message);

            watcher::log_info("processing uid=" + std::to_string(uid) +
                               " from=" + parsed.from_address +
                               " subject=" + parsed.subject);

            // ── Message-ID de-duplication ──────────────────────────────────
            if (!parsed.message_id.empty() &&
                db.is_message_processed(parsed.message_id)) {
                watcher::log_info("skipping duplicate message_id=" + parsed.message_id);
                continue;
            }

            // ── Check for YES reply to a pending print job ─────────────────
            auto pending = db.find_print_job_by_reply(
                parsed.in_reply_to, parsed.references, parsed.subject);

            if (pending.has_value()) {
                if (body_says_yes(parsed.body_text)) {
                    auto pdf_data = read_file(pending->pdf_path);
                    watcher::PrintResult pr = watcher::print_pdf(
                        config.printer_name,
                        fs::path(pending->pdf_path).filename().string(),
                        pdf_data);

                    if (pr.success) {
                        do_reply(smtp, config.gmail.address, pending->from_address,
                                 strings.get("email.reply_printed_subject"),
                                 strings.get("email.reply_printed_body"),
                                 {}, "", pending->confirm_message_id);
                    } else {
                        // Spooler failed — send the PDF back as fallback.
                        mime::Attachment fallback;
                        fallback.filename = fs::path(pending->pdf_path).filename().string();
                        fallback.content_type = "application/pdf";
                        fallback.data = pdf_data;

                        std::string body = strings.get("email.reply_print_failed_body") +
                                           "\n\nDetails: " + pr.error_message;
                        do_reply(smtp, config.gmail.address, pending->from_address,
                                 strings.get("email.reply_print_failed_subject"),
                                 body, {fallback}, "", pending->confirm_message_id);
                    }

                    fs::remove(pending->pdf_path);
                    db.delete_print_job(pending->job_id);
                }
                // If the reply wasn't YES, ignore it — the job stays pending
                // until it expires or a YES arrives.
                if (!parsed.message_id.empty()) {
                    db.mark_message_processed(parsed.message_id);
                }
                continue;
            }

            // ── New incoming request ───────────────────────────────────────

            // Size guard: count total bytes across all attachments.
            std::int64_t total_bytes = 0;
            for (const auto& att : parsed.attachments) {
                total_bytes += static_cast<std::int64_t>(att.data.size());
            }
            const std::int64_t max_bytes = config.max_attachment_mb * 1024 * 1024;
            bool too_large = total_bytes > max_bytes ||
                             static_cast<int>(parsed.attachments.size()) > config.max_attachments;

            if (too_large) {
                do_reply(smtp, config.gmail.address, parsed.from_address,
                         strings.get("email.reply_too_large_subject"),
                         strings.get("email.reply_too_large_body"));
                if (!parsed.message_id.empty()) {
                    db.mark_message_processed(parsed.message_id);
                }
                continue;
            }

            if (parsed.attachments.empty()) {
                do_reply(smtp, config.gmail.address, parsed.from_address,
                         strings.get("email.reply_no_attachment_subject"),
                         strings.get("email.reply_no_attachment_body"));
                if (!parsed.message_id.empty()) {
                    db.mark_message_processed(parsed.message_id);
                }
                continue;
            }

            // Multiple attachments are combined into one ordered PDF — the
            // same behavior as picking multiple files in the desktop UI.
            process_new_message(db, smtp, strings, config, parsed,
                                 tmp_dir, pending_dir, guide_dir, now);

            if (!parsed.message_id.empty()) {
                db.mark_message_processed(parsed.message_id);
            }
        }

        db.save_last_uid(highest_processed);

    } catch (const std::exception& e) {
        watcher::log_error(std::string("run failed: ") + e.what());
        std::cerr << "email-watcher run failed: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return 0;
}
