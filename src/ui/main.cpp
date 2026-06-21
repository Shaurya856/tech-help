// Single-window desktop UI: one decision point (pick or drop a file), then
// automatic routing to either a converted/compressed result or the DOCX
// walkthrough. No settings screen, no format picker.

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <filesystem>
#include <string>

#include "config.h"
#include "process_file.h"
#include "strings.h"

namespace fs = std::filesystem;

namespace {

constexpr int kIdAddFileButton = 101;
constexpr int kIdOpenFileButton = 102;
constexpr int kIdOpenFolderButton = 103;
constexpr int kIdAddAnotherButton = 104;
constexpr int kIdStatusText = 105;
constexpr int kIdDocxText = 106;

HWND g_hwnd = nullptr;
HWND g_addFileButton = nullptr;
HWND g_openFileButton = nullptr;
HWND g_openFolderButton = nullptr;
HWND g_addAnotherButton = nullptr;
HWND g_statusText = nullptr;
HWND g_docxText = nullptr;

core::Strings* g_strings = nullptr;
core::Config* g_config = nullptr;
std::string g_lastOutputPath;
std::string g_lastOutputFolder;

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

std::string to_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, result.data(), size, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

std::string exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path().string();
}

void show_picker(bool show) {
    ShowWindow(g_addFileButton, show ? SW_SHOW : SW_HIDE);
}

void show_result_controls(bool show) {
    ShowWindow(g_statusText, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_openFileButton, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_openFolderButton, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_addAnotherButton, show ? SW_SHOW : SW_HIDE);
}

void show_docx_controls(bool show) {
    ShowWindow(g_docxText, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_addAnotherButton, show ? SW_SHOW : SW_HIDE);
}

void reset_to_picker() {
    show_picker(true);
    show_result_controls(false);
    show_docx_controls(false);
}

std::string move_to_output_folder(const std::string& path) {
    if (g_config->output_folder.empty()) return path;

    std::error_code ec;
    fs::create_directories(g_config->output_folder, ec);
    if (ec) return path;

    fs::path dest = fs::path(g_config->output_folder) / fs::path(path).filename();
    fs::rename(path, dest, ec);
    return ec ? path : dest.string();
}

void handle_file(const std::string& input_path) {
    show_picker(false);

    std::string ext = fs::path(input_path).extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(c));

    if (ext == ".docx") {
        show_docx_controls(true);
        return;
    }

    core::ProcessResult result =
        core::process_file(input_path, g_config->pdf_compression_threshold_mb);

    if (result.status == core::ProcessStatus::Ok) {
        g_lastOutputPath = move_to_output_folder(result.output_path);
        g_lastOutputFolder = fs::path(g_lastOutputPath).parent_path().string();

        std::wstring message = to_wide(g_strings->get("ui.result_success")) + L"\n" +
                                to_wide(g_lastOutputPath);
        SetWindowTextW(g_statusText, message.c_str());
        show_result_controls(true);
    } else if (result.status == core::ProcessStatus::UnsupportedType) {
        SetWindowTextW(g_statusText, to_wide(g_strings->get("ui.error_unsupported")).c_str());
        show_result_controls(true);
        ShowWindow(g_openFileButton, SW_HIDE);
        ShowWindow(g_openFolderButton, SW_HIDE);
    } else {
        SetWindowTextW(g_statusText, to_wide(g_strings->get("ui.error_generic")).c_str());
        show_result_controls(true);
        ShowWindow(g_openFileButton, SW_HIDE);
        ShowWindow(g_openFolderButton, SW_HIDE);
    }
}

void open_file_picker(HWND owner) {
    wchar_t file_buf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Supported files\0*.jpg;*.jpeg;*.png;*.pdf;*.docx\0All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        handle_file(to_utf8(file_buf));
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_addFileButton = CreateWindowW(L"BUTTON", to_wide(g_strings->get("ui.add_file_button")).c_str(),
                WS_VISIBLE | WS_CHILD, 20, 20, 360, 200, hwnd, (HMENU)(INT_PTR)kIdAddFileButton,
                nullptr, nullptr);

            g_statusText = CreateWindowW(L"STATIC", L"", WS_CHILD,
                20, 20, 360, 100, hwnd, (HMENU)(INT_PTR)kIdStatusText, nullptr, nullptr);

            g_openFileButton = CreateWindowW(L"BUTTON", to_wide(g_strings->get("ui.open_file_button")).c_str(),
                WS_CHILD, 20, 130, 170, 30, hwnd, (HMENU)(INT_PTR)kIdOpenFileButton, nullptr, nullptr);

            g_openFolderButton = CreateWindowW(L"BUTTON", to_wide(g_strings->get("ui.open_folder_button")).c_str(),
                WS_CHILD, 210, 130, 170, 30, hwnd, (HMENU)(INT_PTR)kIdOpenFolderButton, nullptr, nullptr);

            g_addAnotherButton = CreateWindowW(L"BUTTON", to_wide(g_strings->get("ui.add_another_button")).c_str(),
                WS_CHILD, 20, 170, 360, 30, hwnd, (HMENU)(INT_PTR)kIdAddAnotherButton, nullptr, nullptr);

            std::wstring docx_message =
                to_wide(g_strings->get("ui.docx_title")) + L"\r\n\r\n" +
                to_wide(g_strings->get("ui.docx_step1")) + L"\r\n" +
                to_wide(g_strings->get("ui.docx_step2")) + L"\r\n" +
                to_wide(g_strings->get("ui.docx_step3")) + L"\r\n" +
                to_wide(g_strings->get("ui.docx_step4"));
            g_docxText = CreateWindowW(L"STATIC", docx_message.c_str(), WS_CHILD,
                20, 20, 360, 150, hwnd, (HMENU)(INT_PTR)kIdDocxText, nullptr, nullptr);

            DragAcceptFiles(hwnd, TRUE);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case kIdAddFileButton:
                    open_file_picker(hwnd);
                    break;
                case kIdOpenFileButton:
                    ShellExecuteW(hwnd, L"open", to_wide(g_lastOutputPath).c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                case kIdOpenFolderButton:
                    ShellExecuteW(hwnd, L"open", to_wide(g_lastOutputFolder).c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                case kIdAddAnotherButton:
                    reset_to_picker();
                    break;
            }
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t path[MAX_PATH];
            if (DragQueryFileW(drop, 0, path, MAX_PATH)) {
                handle_file(to_utf8(path));
            }
            DragFinish(drop);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    std::string base_dir = exe_dir();
    std::string config_path = (fs::path(base_dir) / "config.json").string();

    core::Config config;
    try {
        config = core::load_config(config_path);
    } catch (const std::exception&) {
        // Fall back to defaults (output next to the input file, English UI)
        // so the app still runs if config.json hasn't been set up yet.
    }
    g_config = &config;

    core::Strings strings((fs::path(base_dir) / "lang").string(), config.language);
    g_strings = &strings;

    const wchar_t kClassName[] = L"TechHelpFileAssistant";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName,
        to_wide(g_strings->get("ui.window_title")).c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 280,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
