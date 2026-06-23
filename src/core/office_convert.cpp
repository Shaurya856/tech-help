#include "office_convert.h"

#include <filesystem>
#include <string>

// Windows-specific: CreateProcess + WaitForSingleObject for the 60-second
// hang timeout, TerminateProcess on expiry. Only compiled on Windows (see
// CMakeLists.txt) since core is also built for local macOS process_file
// testing, which doesn't need this file.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace core {

namespace {

std::string last_error_string() {
    DWORD err = GetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    std::string msg = buf;
    while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) {
        msg.pop_back();
    }
    return msg;
}

}  // namespace

OfficeConvertResult office_convert(const std::string& python_exe,
                                    const std::string& convert_script,
                                    const std::string& input_path,
                                    const std::string& output_path) {
    if (python_exe.empty() || convert_script.empty()) {
        return {false, "python_exe or office_convert_script not configured"};
    }
    if (!std::filesystem::exists(python_exe)) {
        return {false, "python_exe not found: " + python_exe};
    }
    if (!std::filesystem::exists(convert_script)) {
        return {false, "convert_script not found: " + convert_script};
    }

    // Build command line: "python_exe" "convert_script" "input_path" "output_path"
    std::string cmd = "\"" + python_exe + "\" \"" + convert_script + "\" "
                    + "\"" + input_path + "\" \"" + output_path + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // Redirect child stdout/stderr to NUL so they don't inherit our console.
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                              OPEN_EXISTING, 0, nullptr);
    si.hStdOutput = nul;
    si.hStdError  = nul;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL created = CreateProcessA(
        nullptr,
        cmd.data(),      // lpCommandLine (non-const, CreateProcessA can modify)
        nullptr, nullptr,
        TRUE,            // bInheritHandles
        0, nullptr, nullptr,
        &si, &pi);

    CloseHandle(nul);

    if (!created) {
        return {false, "CreateProcess failed: " + last_error_string()};
    }

    constexpr DWORD kTimeoutMs = 60000;
    DWORD wait = WaitForSingleObject(pi.hProcess, kTimeoutMs);

    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return {false, "conversion timed out after 60 seconds"};
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) {
        return {false, "convert.py exited with code " + std::to_string(exit_code)};
    }
    if (!std::filesystem::exists(output_path)) {
        return {false, "conversion produced no output file"};
    }
    return {true, {}};
}

}  // namespace core
