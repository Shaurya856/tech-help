// Single-window desktop UI: pick one or more files, reorder if multiple,
// then automatic routing to converted/compressed result. No settings screen,
// no format picker. One screen per decision.

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "config.h"
#include "office_convert.h"
#include "process_file.h"
#include "strings.h"

namespace fs = std::filesystem;

namespace {

// ── Control IDs ───────────────────────────────────────────────────────────
constexpr int kIdAddFileButton    = 101;
constexpr int kIdOpenFileButton   = 102;
constexpr int kIdOpenFolderButton = 103;
constexpr int kIdAddAnotherButton = 104;
constexpr int kIdStatusText       = 105;
constexpr int kIdDocxText         = 106;
constexpr int kIdReorderList      = 107;
constexpr int kIdMoveUpButton     = 108;
constexpr int kIdMoveDownButton   = 109;
constexpr int kIdConvertButton    = 110;
constexpr int kIdCancelButton     = 111;
constexpr int kIdReorderLabel     = 112;

// ── Globals ────────────────────────────────────────────────────────────────
HWND g_hwnd             = nullptr;
HWND g_addFileButton    = nullptr;
HWND g_openFileButton   = nullptr;
HWND g_openFolderButton = nullptr;
HWND g_addAnotherButton = nullptr;
HWND g_statusText       = nullptr;
HWND g_docxText         = nullptr;
HWND g_reorderList      = nullptr;
HWND g_moveUpButton     = nullptr;
HWND g_moveDownButton   = nullptr;
HWND g_convertButton    = nullptr;
HWND g_cancelButton     = nullptr;
HWND g_reorderLabel     = nullptr;

core::Strings* g_strings = nullptr;
core::Config*  g_config  = nullptr;

std::string g_lastOutputPath;
std::string g_lastOutputFolder;
std::vector<std::string> g_pendingFiles;  // files to convert (mutable order)

// ── String helpers ─────────────────────────────────────────────────────────
std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring r(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, r.data(), sz);
    if (!r.empty() && r.back() == L'\0') r.pop_back();
    return r;
}

std::string to_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string r(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, r.data(), sz, nullptr, nullptr);
    if (!r.empty() && r.back() == '\0') r.pop_back();
    return r;
}

// ── Filesystem helpers ─────────────────────────────────────────────────────
std::string exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path().string();
}

std::string extension_of(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(tolower(c));
    return ext;
}

std::string get_tmp_dir() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    fs::path p = fs::path(tmp) / L"TechHelpUI";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}

std::string timestamp_suffix() {
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(sec);
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

// ── UI state transitions ───────────────────────────────────────────────────
void hide_all() {
    for (HWND h : {g_addFileButton, g_openFileButton, g_openFolderButton,
                    g_addAnotherButton, g_statusText, g_docxText,
                    g_reorderList, g_moveUpButton, g_moveDownButton,
                    g_convertButton, g_cancelButton, g_reorderLabel}) {
        ShowWindow(h, SW_HIDE);
    }
}

void show_picker() {
    hide_all();
    ShowWindow(g_addFileButton, SW_SHOW);
}

void show_result(bool success) {
    hide_all();
    ShowWindow(g_statusText, SW_SHOW);
    if (success) {
        ShowWindow(g_openFileButton, SW_SHOW);
        ShowWindow(g_openFolderButton, SW_SHOW);
    }
    ShowWindow(g_addAnotherButton, SW_SHOW);
}

void show_docx_guide() {
    hide_all();
    ShowWindow(g_docxText, SW_SHOW);
    ShowWindow(g_addAnotherButton, SW_SHOW);
}

void show_reorder() {
    hide_all();
    // Populate the listbox.
    SendMessage(g_reorderList, LB_RESETCONTENT, 0, 0);
    for (const auto& f : g_pendingFiles) {
        SendMessageW(g_reorderList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(to_wide(fs::path(f).filename().string()).c_str()));
    }
    ShowWindow(g_reorderLabel,  SW_SHOW);
    ShowWindow(g_reorderList,   SW_SHOW);
    ShowWindow(g_moveUpButton,  SW_SHOW);
    ShowWindow(g_moveDownButton,SW_SHOW);
    ShowWindow(g_convertButton, SW_SHOW);
    ShowWindow(g_cancelButton,  SW_SHOW);
}

// ── Multi-file processing ──────────────────────────────────────────────────
// Converts a DOCX/XLSX to PDF via Word/Excel COM automation (same mechanism
// as the email watcher) and compresses the result. Returns the final PDF
// path, or empty string if Office automation isn't configured or fails —
// callers should fall back to the manual Print-to-PDF guide in that case.
std::string convert_office_to_pdf(const std::string& input_path) {
    const std::string tmp = get_tmp_dir();
    const std::string office_out =
        (fs::path(tmp) / (fs::path(input_path).stem().string() + "_converted.pdf")).string();

    core::OfficeConvertResult result = core::office_convert(
        g_config->python_exe, g_config->office_convert_script, input_path, office_out);
    if (!result.success) return "";

    core::ProcessResult compressed = core::process_file(office_out);
    return compressed.status == core::ProcessStatus::Ok ? compressed.output_path : office_out;
}

// Processes each file to a temp PDF, then combines them into one.
// Returns the combined PDF path, or empty string on failure.
std::string combine_selected_files() {
    std::string tmp = get_tmp_dir();
    std::vector<std::string> temp_pdfs;

    for (const auto& input : g_pendingFiles) {
        const std::string ext = extension_of(input);
        if (!core::is_image_ext(ext) && ext != ".pdf" && !core::is_office_ext(ext)) continue;

        // Copy input to tmp so we don't modify the original and the
        // output goes to a predictable temp location.
        fs::path tmp_input = fs::path(tmp) / fs::path(input).filename();
        std::error_code ec;
        fs::copy_file(input, tmp_input, fs::copy_options::overwrite_existing, ec);
        if (ec) continue;

        if (core::is_office_ext(ext)) {
            std::string converted = convert_office_to_pdf(tmp_input.string());
            if (!converted.empty()) temp_pdfs.push_back(converted);
            continue;
        }

        core::ProcessResult result = core::process_file(tmp_input.string());
        if (result.status == core::ProcessStatus::Ok) {
            temp_pdfs.push_back(result.output_path);
        }
    }

    if (temp_pdfs.empty()) return "";
    if (temp_pdfs.size() == 1) return temp_pdfs[0];

    // Combine into one PDF.
    const std::string stem = fs::path(g_pendingFiles[0]).stem().string();
    const std::string combined_path =
        (fs::path(tmp) / (stem + "_combined_" + timestamp_suffix() + ".pdf")).string();

    if (!core::combine_pdfs(temp_pdfs, combined_path)) return "";

    // Clean up the per-file temp PDFs (but not the combined result).
    for (const auto& p : temp_pdfs) {
        std::error_code ec2;
        fs::remove(p, ec2);
    }

    return combined_path;
}

// ── Single-file handler ───────────────────────────────────────────────────
void handle_single_file(const std::string& input_path) {
    const std::string ext = extension_of(input_path);

    SetWindowTextW(g_statusText,
                    to_wide(g_strings->get("ui.processing")).c_str());
    ShowWindow(g_statusText, SW_SHOW);
    UpdateWindow(g_hwnd);

    if (core::is_office_ext(ext)) {
        std::string converted = convert_office_to_pdf(input_path);
        if (converted.empty()) {
            // Office automation not configured/failed — fall back to the
            // manual Print-to-PDF walkthrough rather than blocking him.
            show_docx_guide();
            return;
        }
        g_lastOutputPath   = move_to_output_folder(converted);
        g_lastOutputFolder = fs::path(g_lastOutputPath).parent_path().string();

        std::wstring msg = to_wide(g_strings->get("ui.result_success")) + L"\n" +
                            to_wide(g_lastOutputPath);
        SetWindowTextW(g_statusText, msg.c_str());
        show_result(true);
        return;
    }

    core::ProcessResult result = core::process_file(input_path);

    if (result.status == core::ProcessStatus::Ok) {
        g_lastOutputPath   = move_to_output_folder(result.output_path);
        g_lastOutputFolder = fs::path(g_lastOutputPath).parent_path().string();

        std::wstring msg = to_wide(g_strings->get("ui.result_success")) + L"\n" +
                            to_wide(g_lastOutputPath);
        SetWindowTextW(g_statusText, msg.c_str());
        show_result(true);
    } else if (result.status == core::ProcessStatus::UnsupportedType) {
        SetWindowTextW(g_statusText, to_wide(g_strings->get("ui.error_unsupported")).c_str());
        show_result(false);
    } else {
        SetWindowTextW(g_statusText, to_wide(g_strings->get("ui.error_generic")).c_str());
        show_result(false);
    }
}

// ── Multi-file handler ────────────────────────────────────────────────────
void handle_convert_clicked() {
    SetWindowTextW(g_statusText, to_wide(g_strings->get("ui.processing")).c_str());
    show_result(false);  // show status text + add-another; hide reorder
    ShowWindow(g_statusText, SW_SHOW);
    UpdateWindow(g_hwnd);

    std::string combined = combine_selected_files();

    if (!combined.empty()) {
        g_lastOutputPath   = move_to_output_folder(combined);
        g_lastOutputFolder = fs::path(g_lastOutputPath).parent_path().string();

        std::wstring msg = to_wide(g_strings->get("ui.result_success")) + L"\n" +
                            to_wide(g_lastOutputPath);
        SetWindowTextW(g_statusText, msg.c_str());
        show_result(true);
    } else {
        SetWindowTextW(g_statusText, to_wide(g_strings->get("ui.error_generic")).c_str());
        show_result(false);
    }

    g_pendingFiles.clear();
}

// ── Reorder list helpers ──────────────────────────────────────────────────
void reorder_move(int delta) {
    int sel = static_cast<int>(SendMessage(g_reorderList, LB_GETCURSEL, 0, 0));
    if (sel == LB_ERR) return;
    int next = sel + delta;
    if (next < 0 || next >= static_cast<int>(g_pendingFiles.size())) return;

    std::swap(g_pendingFiles[sel], g_pendingFiles[next]);

    SendMessage(g_reorderList, LB_RESETCONTENT, 0, 0);
    for (const auto& f : g_pendingFiles) {
        SendMessageW(g_reorderList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(to_wide(fs::path(f).filename().string()).c_str()));
    }
    SendMessage(g_reorderList, LB_SETCURSEL, next, 0);
}

// ── File picker ───────────────────────────────────────────────────────────
void open_file_picker(HWND owner) {
    // OFN_ALLOWMULTISELECT requires a larger buffer — allocate 32 KB for
    // Explorer-mode multi-select (null-delimited list of paths).
    constexpr int kBufSize = 32768;
    std::vector<wchar_t> buf(kBufSize, L'\0');

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = kBufSize;
    ofn.lpstrFilter =
        L"Supported files\0"
        L"*.jpg;*.jpeg;*.png;*.bmp;*.webp;*.tga;*.gif;*.tif;*.tiff;*.heic;*.heif;*.pdf;*.docx;*.xlsx\0"
        L"All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn)) return;

    // Parse the buffer. Explorer multi-select format:
    //   <dir>\0<file1>\0<file2>\0...\0\0   (if multiple)
    //   <full_path>\0\0                    (if single)
    std::vector<std::string> selected;
    wchar_t* p = buf.data();
    std::wstring dir = p;
    p += dir.size() + 1;

    if (*p == L'\0') {
        // Single file — dir is actually the full path.
        selected.push_back(to_utf8(dir));
    } else {
        while (*p != L'\0') {
            std::wstring name = p;
            selected.push_back(to_utf8((fs::path(dir) / name).wstring()));
            p += name.size() + 1;
        }
    }

    if (selected.empty()) return;

    if (selected.size() == 1) {
        handle_single_file(selected[0]);
    } else {
        g_pendingFiles = std::move(selected);
        show_reorder();
    }
}

// ── Drop handler ──────────────────────────────────────────────────────────
void handle_drop(HDROP drop) {
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count == 0) { DragFinish(drop); return; }

    if (count == 1) {
        wchar_t path[MAX_PATH];
        DragQueryFileW(drop, 0, path, MAX_PATH);
        DragFinish(drop);
        handle_single_file(to_utf8(path));
        return;
    }

    std::vector<std::string> files;
    for (UINT i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(drop, i, path, MAX_PATH)) {
            files.push_back(to_utf8(path));
        }
    }
    DragFinish(drop);

    if (!files.empty()) {
        g_pendingFiles = std::move(files);
        show_reorder();
    }
}

// ── Window procedure ───────────────────────────────────────────────────────
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

        case WM_CREATE: {
            // Picker
            g_addFileButton = CreateWindowW(L"BUTTON",
                to_wide(g_strings->get("ui.add_file_button")).c_str(),
                WS_VISIBLE | WS_CHILD,
                20, 20, 360, 200, hwnd, (HMENU)(INT_PTR)kIdAddFileButton, nullptr, nullptr);

            // Result
            g_statusText = CreateWindowW(L"STATIC", L"", WS_CHILD,
                20, 20, 360, 100, hwnd, (HMENU)(INT_PTR)kIdStatusText, nullptr, nullptr);

            g_openFileButton = CreateWindowW(L"BUTTON",
                to_wide(g_strings->get("ui.open_file_button")).c_str(),
                WS_CHILD, 20, 130, 170, 30, hwnd, (HMENU)(INT_PTR)kIdOpenFileButton, nullptr, nullptr);

            g_openFolderButton = CreateWindowW(L"BUTTON",
                to_wide(g_strings->get("ui.open_folder_button")).c_str(),
                WS_CHILD, 210, 130, 170, 30, hwnd, (HMENU)(INT_PTR)kIdOpenFolderButton, nullptr, nullptr);

            g_addAnotherButton = CreateWindowW(L"BUTTON",
                to_wide(g_strings->get("ui.add_another_button")).c_str(),
                WS_CHILD, 20, 170, 360, 30, hwnd, (HMENU)(INT_PTR)kIdAddAnotherButton, nullptr, nullptr);

            // DOCX guide
            std::wstring docx_msg =
                to_wide(g_strings->get("ui.docx_title")) + L"\r\n\r\n" +
                to_wide(g_strings->get("ui.docx_step1")) + L"\r\n" +
                to_wide(g_strings->get("ui.docx_step2")) + L"\r\n" +
                to_wide(g_strings->get("ui.docx_step3")) + L"\r\n" +
                to_wide(g_strings->get("ui.docx_step4"));
            g_docxText = CreateWindowW(L"STATIC", docx_msg.c_str(), WS_CHILD,
                20, 20, 360, 150, hwnd, (HMENU)(INT_PTR)kIdDocxText, nullptr, nullptr);

            // Reorder screen
            g_reorderLabel = CreateWindowW(L"STATIC", L"Drag to reorder, then click Convert:",
                WS_CHILD, 20, 20, 360, 20, hwnd, (HMENU)(INT_PTR)kIdReorderLabel, nullptr, nullptr);

            g_reorderList = CreateWindowW(L"LISTBOX", nullptr,
                WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                20, 48, 360, 200, hwnd, (HMENU)(INT_PTR)kIdReorderList, nullptr, nullptr);

            g_moveUpButton = CreateWindowW(L"BUTTON", L"▲ Move Up",
                WS_CHILD, 20, 258, 170, 28, hwnd, (HMENU)(INT_PTR)kIdMoveUpButton, nullptr, nullptr);

            g_moveDownButton = CreateWindowW(L"BUTTON", L"▼ Move Down",
                WS_CHILD, 210, 258, 170, 28, hwnd, (HMENU)(INT_PTR)kIdMoveDownButton, nullptr, nullptr);

            g_convertButton = CreateWindowW(L"BUTTON", L"Convert to PDF",
                WS_CHILD, 20, 296, 360, 32, hwnd, (HMENU)(INT_PTR)kIdConvertButton, nullptr, nullptr);

            g_cancelButton = CreateWindowW(L"BUTTON", L"Cancel",
                WS_CHILD, 20, 338, 360, 28, hwnd, (HMENU)(INT_PTR)kIdCancelButton, nullptr, nullptr);

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
                    g_pendingFiles.clear();
                    show_picker();
                    break;
                case kIdMoveUpButton:
                    reorder_move(-1);
                    break;
                case kIdMoveDownButton:
                    reorder_move(+1);
                    break;
                case kIdConvertButton:
                    handle_convert_clicked();
                    break;
                case kIdCancelButton:
                    g_pendingFiles.clear();
                    show_picker();
                    break;
            }
            return 0;
        }

        case WM_DROPFILES: {
            handle_drop(reinterpret_cast<HDROP>(wParam));
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
    const std::string base_dir    = exe_dir();
    const std::string config_path = (fs::path(base_dir) / "config.json").string();

    core::Config config;
    try {
        config = core::load_config(config_path);
    } catch (const std::exception&) {
        // Fall back to defaults if config.json not set up yet.
    }
    g_config = &config;

    core::Strings strings((fs::path(base_dir) / "lang").string(), config.language);
    g_strings = &strings;

    const wchar_t kClassName[] = L"TechHelpFileAssistant";
    WNDCLASSW wc{};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kClassName;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, kClassName,
        to_wide(strings.get("ui.window_title")).c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 390,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(g_hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
