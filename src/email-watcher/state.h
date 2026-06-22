#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace watcher {

struct PendingPrintJob {
    std::string job_id;
    // Message-ID of the confirmation email we sent — matched against
    // In-Reply-To/References when dad replies YES.
    std::string confirm_message_id;
    std::string from_address;
    // Bare subject of the confirmation email (for fallback subject matching).
    std::string confirm_subject;
    std::string pdf_path;
    std::int64_t created_at;  // unix seconds
};

class WatcherDB {
public:
    explicit WatcherDB(const std::string& db_path);
    ~WatcherDB();

    WatcherDB(const WatcherDB&) = delete;
    WatcherDB& operator=(const WatcherDB&) = delete;

    // ── Message-ID de-duplication ──────────────────────────────────────────
    bool is_message_processed(const std::string& message_id) const;
    void mark_message_processed(const std::string& message_id);

    // ── UID watermark (replaces the old JSON state file) ──────────────────
    std::optional<std::int64_t> load_last_uid() const;
    void save_last_uid(std::int64_t uid);

    // ── Pending print jobs ────────────────────────────────────────────────
    void save_print_job(const PendingPrintJob& job);
    // Checks In-Reply-To, References (each Message-ID in the space-separated
    // list), then bare subject as fallback. Returns the first match.
    std::optional<PendingPrintJob> find_print_job_by_reply(
        const std::string& in_reply_to,
        const std::string& references,
        const std::string& subject) const;
    std::vector<PendingPrintJob> get_expired_jobs(std::int64_t cutoff_unix) const;
    void delete_print_job(const std::string& job_id);

private:
    sqlite3* db_ = nullptr;

    void exec(const char* sql) const;
};

}  // namespace watcher
