# tech-help

A local-first file assistant: converts images to PDF, compresses oversized PDFs,
and routes anything it can't handle (DOCX) to a simple guide — usable from a
desktop app, by email, or via an MCP tool for Claude Cowork.

## Status

All pieces in the spec are implemented:

- `core::process_file` — image→PDF (libharu) and PDF compression (QPDF), verified locally on macOS by compiling/linking against the real libraries and running it.
- `core-cli` — thin CLI wrapper around `process_file` for non-C++ callers.
- `ui` — native Win32 desktop app (file picker, drag-and-drop, result screen, DOCX walkthrough). Windows-only API; not compilable on macOS, so this has only been reviewed, not built — first real build/test happens in CI.
- `email-watcher` — IMAP fetch / MIME attachment parsing / SMTP reply, built on libcurl. Compiled, linked, and run locally against a fake host to confirm the config → IMAP → error-handling path works; the MIME encode/decode logic has dedicated round-trip tests. The actual protocol exchange against a real Gmail account is untested.
- `mcp-server` — Python MCP server (`process_file` + `summarize_file` tools), verified locally: server imports, tools run end-to-end against synthetic config/PDF/CLI stand-ins.
- `skills/` — both SKILL.md files written.
- `install/install.ps1` — full installer (copy binaries, Python venv, Task Scheduler, Claude Desktop MCP registration). Windows-only, not run locally.

**Known gaps, by design or by necessity:**
- `assets/docx-guide` has no real screenshots — those need to be captured by a human walking through Word's Print-to-PDF flow on Windows.
- PDF compression only recompresses container structure (QPDF), not embedded image quality/resolution — a PDF full of large photos may still exceed the threshold after compression.
- Image OCR in `summarize_file` requires the Tesseract OCR engine installed separately on the target machine (see `src/mcp-server/README.md`) — the one external-binary dependency in the project, since it doesn't fit the "no external app" constraint for video/PDF conversion.

## Repository layout

```
/src
  /core          - process_file, config, localization strings (C++)
  /core-cli      - CLI wrapper around process_file for non-C++ callers
  /ui            - native Win32 desktop UI app
  /email-watcher - scheduled IMAP/SMTP checker (libcurl)
  /mcp-server    - MCP server (Python) exposing process_file + summarize_file
/lang
  en.json, template.json - localization strings
/assets
  /docx-guide    - screenshot images for the Word print-to-PDF walkthrough (placeholder, needs real screenshots)
/install
  install.ps1
  config.example.json
/skills
  conversion-skill/SKILL.md
  daily-sync-skill/SKILL.md
/.github/workflows
  build.yml
```

## Build

Builds run on `windows-latest` via GitHub Actions (`.github/workflows/build.yml`)
on every push to `main` and on `v*` tags, using vcpkg (preinstalled on the
runner) to resolve libharu/qpdf/curl/nlohmann-json. No local Windows toolchain
or cross-compilation setup is needed — push, then pull the `.exe` artifacts
from the Actions run (or the GitHub Release, for tagged builds).

To configure locally for development (any platform with CMake, a C++17
compiler, and vcpkg):

```
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

The MCP server is a separate Python project:

```
cd src/mcp-server
python3 -m venv venv
source venv/bin/activate   # Windows: venv\Scripts\activate
pip install -r requirements.txt
python3 server.py
```

## Configuration

Copy `install/config.example.json` to `config.json` and fill in real values
(Gmail credentials, folder paths, LLM API key). `config.json` is git-ignored
and must be transferred to the target machine separately — `install.ps1`
takes it as a parameter and copies it into place for both the compiled
binaries and the MCP server.

## Install

On the target Windows machine, after downloading the built binaries:

```
.\install.ps1 -SourceDir .\dist -ConfigPath .\config.json
```

See the script's output for the remaining manual steps (Cowork folder
permissions, model setting, Skill upload, daily schedule).
