#include "log.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace watcher {

namespace {

namespace fs = std::filesystem;

constexpr std::uintmax_t kRotateSizeBytes = 5 * 1024 * 1024;  // 5 MB

std::string g_log_path;

std::string log_dir_from_env() {
    char buf[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return fs::current_path().string();
    return std::string(buf) + "\\FatherTechAssist\\logs";
}

std::string timestamp_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void rotate_if_needed() {
    if (g_log_path.empty()) return;
    std::error_code ec;
    auto sz = fs::file_size(g_log_path, ec);
    if (!ec && sz >= kRotateSizeBytes) {
        std::string backup = g_log_path + ".1";
        fs::rename(g_log_path, backup, ec);
    }
}

void write_line(const std::string& level, const std::string& msg) {
    if (g_log_path.empty()) return;
    rotate_if_needed();
    std::ofstream f(g_log_path, std::ios::app);
    if (f) f << "[" << timestamp_now() << "] [" << level << "] " << msg << "\n";
}

}  // namespace

void log_init() {
    std::string dir = log_dir_from_env();
    std::error_code ec;
    fs::create_directories(dir, ec);
    g_log_path = (fs::path(dir) / "watcher.log").string();
}

void log_info(const std::string& msg) {
    write_line("INFO", msg);
}

void log_error(const std::string& msg) {
    write_line("ERROR", msg);
}

}  // namespace watcher
