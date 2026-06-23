# tech-help

A local-first file assistant: converts images/PDFs/DOCX/XLSX to compressed
PDFs, handles print-by-email with a confirmation step, keeps a searchable
daily index of three folders via a local agent, and exposes the same
conversion core from a desktop app, by email, or as MCP tools for Claude
Cowork.

## Status

All pieces in the spec (`spec.md`) are implemented:

- `core::process_file` — image→PDF (libharu) and PDF compression (QPDF),
  plus `reorder_pdf`/`combine_pdfs` for multi-file assembly. Verified
  locally on macOS by compiling/linking against the real libraries.
- `core-cli` — thin CLI wrapper around `process_file` and the `reorder`
  subcommand, for non-C++ callers (MCP server).
- `ui` — native Win32 desktop app: single-file drop/pick, or multi-file
  select with a drag-to-reorder screen before combining into one PDF.
  Windows-only API; not compilable on macOS — first real build/test
  happens in CI.
- `email-watcher` — IMAP fetch / MIME threading (Message-ID, In-Reply-To,
  References) / SMTP reply, built on libcurl + SQLite3. Handles:
  size/attachment-count limits, Message-ID de-duplication, `name: <text>`
  subject parsing for output filenames, no-attachment replies, and the
  print-via-email confirmation flow (preview reply → YES → spooler print,
  with a 24-hour expiry sweep). Compiled and run locally against a fake
  host to confirm the config → IMAP → error-handling path; the MIME
  encode/decode logic has dedicated round-trip tests. The actual protocol
  exchange against a real Gmail account is untested.
- **DOCX/XLSX → PDF conversion is uniform across all three entry points**
  (email, desktop UI, MCP/Cowork): each calls `core::office_convert`
  (`src/core/office_convert.h`), which spawns `office/convert.py` —
  Word/Excel COM automation via `win32com.client` — with a 60-second hang
  timeout, then compresses the resulting PDF through the normal
  `process_file` path. If Office automation isn't configured or fails on
  the target machine, all three fall back identically to the manual
  "open in Word → Print → Microsoft Print to PDF" guide rather than
  blocking. `convert.py` itself is never imported directly into C++ or the
  MCP server — always spawned as a subprocess.
- `mcp-server` — Python MCP server exposing `process_file`,
  `summarize_file`, `search_file_index`, and `set_page_order`.
  `process_file` routes DOCX/XLSX through the same `convert.py` subprocess
  as email/UI before compressing, returning `status: "unsupported"` only
  if Office automation isn't available. Verified locally: server imports,
  tools run end-to-end against synthetic config/PDF/CLI stand-ins.
- `skills/` — `conversion-skill` (MCP-driven conversion/reorder requests)
  and `daily-index-skill` (Hermes Agent invocation + index search) written.
- `install/install.ps1` — full installer: copies binaries, sets up two
  Python venvs (MCP server, Office COM), patches `config.json` with
  resolved paths, registers the Task Scheduler entry and Claude Desktop
  MCP server, and applies OS-level ACLs (read-only on source folders,
  write-only on the index folder) for Hermes Agent. Windows-only, not run
  locally.

**Known gaps, by design or by necessity:**
- `assets/docx-guide` has no real screenshots — not needed in the current
  flow (DOCX/XLSX now convert via COM automation rather than a manual
  Word walkthrough), kept only as a fallback reference.
- DOCX/XLSX conversion requires Microsoft Office installed on the target
  machine — Word/Excel COM automation is a hard dependency for those two
  formats specifically (see spec §11).
- The Hermes Agent CLI invocation in `daily-index-skill/SKILL.md` uses an
  assumed CLI shape (`hermes run --scan-paths ... --output-dir ...`) —
  this needs to be verified against the actual Hermes Agent installation
  and updated if the real CLI differs.
- The UI's multi-file reorder screen shows filenames only, no thumbnails —
  would need GDI+ (images) or a PDF renderer for visual previews.
- `icacls /deny` on source folders (for Hermes's read-only boundary) also
  blocks the user's own writes to those folders from Explorer; recovery
  command is printed at the end of `install.ps1`.

## Repository layout

```
/src
  /core          - process_file, reorder/combine PDFs, config, localization (C++)
  /core-cli      - CLI wrapper around process_file + reorder, for non-C++ callers
  /ui            - native Win32 desktop UI, single- and multi-file flows
  /email-watcher - scheduled IMAP/SMTP checker, SQLite state, print confirmation (C++)
  /office        - Word/Excel COM automation (Python, spawned by email-watcher)
  /ordering      - reserved for multi-file ordering logic (currently routed through core-cli)
  /mcp-server    - MCP server (Python): process_file, summarize_file,
                   search_file_index, set_page_order
/lang
  en.json, template.json - localization strings
/assets
  /docx-guide    - legacy Word print-to-PDF screenshots (fallback reference only)
/install
  install.ps1
  config.example.json
/skills
  conversion-skill/SKILL.md
  daily-index-skill/SKILL.md
/.github/workflows
  build.yml
```

## Build

Builds run on `windows-latest` via GitHub Actions (`.github/workflows/build.yml`)
on every push to `main` and on `v*` tags, using vcpkg (preinstalled on the
runner) to resolve libharu/qpdf/curl/nlohmann-json/tiff/libheif/sqlite3. No
local Windows toolchain or cross-compilation setup is needed — push, then
pull the `.exe` artifacts from the Actions run (or the GitHub Release, for
tagged builds). This project cannot be built locally on macOS/Linux — the
email watcher and UI use Win32 APIs (`CreateProcess`, `Winspool.lib`,
`localtime_s`) throughout.

To configure on Windows for local development (CMake, MSVC, and vcpkg):

```
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_OVERLAY_TRIPLETS=.\vcpkg-triplets
cmake --build build --config Release
```

The MCP server and Office COM script are separate Python projects:

```
cd src/mcp-server
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
python server.py
```

```
cd src/office
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
```

## Configuration

Copy `install/config.example.json` to `config.json` and fill in real values
(Gmail App Password, printer name, three source folder paths, output
folder, size/attachment limits, LLM provider API keys). `config.json` is
git-ignored and must be transferred to the target machine separately —
`install.ps1` takes it as a parameter, patches in the resolved
`python_exe`/`office_convert_script` paths after creating the Office venv,
and copies it into place for both the compiled binaries and the MCP server.

## Install

On the target Windows machine, after downloading the built binaries:

```
.\install.ps1 -SourceDir .\dist -ConfigPath .\config.json
```

See the script's output for the remaining manual steps (Cowork folder
permissions, model setting, Skill upload, daily schedule, and the ACL
recovery command if you need to write to a source folder yourself).
