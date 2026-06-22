#include "state.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <sqlite3.h>

namespace watcher {

namespace {

// Strip leading "Re: " (case-insensitive, repeated) from a subject line.
std::string strip_re(const std::string& s) {
    std::string out = s;
    while (out.size() >= 4) {
        if ((out[0] == 'R' || out[0] == 'r') &&
            (out[1] == 'e' || out[1] == 'E') &&
             out[2] == ':' && out[3] == ' ') {
            out = out.substr(4);
        } else {
            break;
        }
    }
    return out;
}

// Split a References header into individual Message-IDs.
// Format: "<id1> <id2> <id3>" — extract everything inside angle brackets.
std::vector<std::string> split_references(const std::string& refs) {
    std::vector<std::string> ids;
    size_t pos = 0;
    while (pos < refs.size()) {
        size_t lt = refs.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = refs.find('>', lt);
        if (gt == std::string::npos) break;
        ids.push_back(refs.substr(lt, gt - lt + 1));
        pos = gt + 1;
    }
    return ids;
}

}  // namespace

WatcherDB::WatcherDB(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("sqlite3_open: " + msg);
    }
    exec("PRAGMA journal_mode=WAL;");
    exec(R"(
        CREATE TABLE IF NOT EXISTS processed_messages (
            message_id TEXT PRIMARY KEY
        );
        CREATE TABLE IF NOT EXISTS pending_print_jobs (
            job_id             TEXT PRIMARY KEY,
            confirm_message_id TEXT NOT NULL,
            from_address       TEXT NOT NULL,
            confirm_subject    TEXT NOT NULL,
            pdf_path           TEXT NOT NULL,
            created_at         INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS settings (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");
}

WatcherDB::~WatcherDB() {
    if (db_) sqlite3_close(db_);
}

void WatcherDB::exec(const char* sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error(std::string("sqlite exec: ") + msg);
    }
}

bool WatcherDB::is_message_processed(const std::string& message_id) const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT 1 FROM processed_messages WHERE message_id=? LIMIT 1",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, message_id.c_str(), -1, SQLITE_STATIC);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

void WatcherDB::mark_message_processed(const std::string& message_id) {
    if (message_id.empty()) return;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT OR IGNORE INTO processed_messages(message_id) VALUES(?)",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, message_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<std::int64_t> WatcherDB::load_last_uid() const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT value FROM settings WHERE key='last_uid' LIMIT 1",
        -1, &stmt, nullptr);
    std::optional<std::int64_t> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

void WatcherDB::save_last_uid(std::int64_t uid) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT OR REPLACE INTO settings(key, value) VALUES('last_uid', ?)",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, uid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void WatcherDB::save_print_job(const PendingPrintJob& job) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT OR REPLACE INTO pending_print_jobs"
        "(job_id, confirm_message_id, from_address, confirm_subject, pdf_path, created_at)"
        " VALUES(?,?,?,?,?,?)",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, job.job_id.c_str(),             -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, job.confirm_message_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, job.from_address.c_str(),       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, job.confirm_subject.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, job.pdf_path.c_str(),           -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, job.created_at);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<PendingPrintJob> WatcherDB::find_print_job_by_reply(
    const std::string& in_reply_to,
    const std::string& references,
    const std::string& subject) const {

    // Load all pending jobs (at most a handful) and match in C++.
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT job_id, confirm_message_id, from_address, confirm_subject, pdf_path, created_at"
        " FROM pending_print_jobs",
        -1, &stmt, nullptr);

    // Collect candidate IDs from threading headers.
    std::vector<std::string> candidate_ids;
    if (!in_reply_to.empty()) {
        auto ids = split_references(in_reply_to);
        candidate_ids.insert(candidate_ids.end(), ids.begin(), ids.end());
        // Also treat the raw value as a candidate (if not angle-bracketed).
        candidate_ids.push_back(in_reply_to);
    }
    if (!references.empty()) {
        auto ids = split_references(references);
        candidate_ids.insert(candidate_ids.end(), ids.begin(), ids.end());
    }

    std::string bare_subject = strip_re(subject);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingPrintJob job;
        job.job_id             = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        job.confirm_message_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        job.from_address       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        job.confirm_subject    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        job.pdf_path           = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        job.created_at         = sqlite3_column_int64(stmt, 5);

        bool match = false;
        for (const auto& cid : candidate_ids) {
            if (cid.find(job.confirm_message_id) != std::string::npos ||
                job.confirm_message_id.find(cid) != std::string::npos) {
                match = true;
                break;
            }
        }
        if (!match && !bare_subject.empty() && bare_subject == job.confirm_subject) {
            match = true;
        }

        if (match) {
            sqlite3_finalize(stmt);
            return job;
        }
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<PendingPrintJob> WatcherDB::get_expired_jobs(std::int64_t cutoff_unix) const {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT job_id, confirm_message_id, from_address, confirm_subject, pdf_path, created_at"
        " FROM pending_print_jobs WHERE created_at < ?",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, cutoff_unix);

    std::vector<PendingPrintJob> jobs;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingPrintJob job;
        job.job_id             = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        job.confirm_message_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        job.from_address       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        job.confirm_subject    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        job.pdf_path           = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        job.created_at         = sqlite3_column_int64(stmt, 5);
        jobs.push_back(std::move(job));
    }
    sqlite3_finalize(stmt);
    return jobs;
}

void WatcherDB::delete_print_job(const std::string& job_id) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
        "DELETE FROM pending_print_jobs WHERE job_id=?",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, job_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

}  // namespace watcher
