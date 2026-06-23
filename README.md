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

## Install (step by step, on the target Windows laptop)

This repo cannot be built or run on macOS/Linux. You need two separate
downloads from GitHub — the **build artifact** (the exes) and the **source
zip** (everything else) — plus a few prerequisites already on the machine.

### 0. Prerequisites

- **Python 3.12** (not 3.14 — some dependencies like `pywin32` and
  `pillow-heif` may not have prebuilt wheels for very new Python versions
  yet). Get it from python.org, and check **"Add python.exe to PATH"**
  during install. Verify with `python --version`.
- **Microsoft Word + Excel** installed — required for DOCX/XLSX conversion
  (COM automation, no substitute).
- A configured **printer** — note its exact Windows printer name.
- **Claude Desktop** installed, if you're using the MCP/Cowork integration.

### 1. Get the build artifact

If this repo has a GitHub remote with a passing Actions run: go to the
repo's **Actions** tab → latest successful run → download the
`tech-help-windows` artifact and extract it. You should have exactly:
`ui.exe`, `email-watcher.exe`, `core-cli.exe`.

(If there's no CI run available, you'd need a full Visual Studio + vcpkg
toolchain to build locally — see the **Build** section above. Not needed if
you already have CI artifacts.)

### 2. Get the source

On the repo's GitHub page: **Code** (green button) → **Download ZIP** →
extract it. This gives you `lang/`, `assets/docx-guide/`, `src/mcp-server/`,
`src/office/`, `install/install.ps1`, and `install/config.example.json`.

### 3. Assemble the `dist` folder

`install.ps1` expects one folder containing everything together. From
PowerShell, `cd` into the extracted source folder, then (adjust `$exeDir`
to wherever you extracted the build artifact):

```powershell
$exeDir = "C:\path\to\extracted\tech-help-windows"

New-Item -ItemType Directory -Force -Path dist | Out-Null
Copy-Item "$exeDir\ui.exe","$exeDir\email-watcher.exe","$exeDir\core-cli.exe" -Destination dist
Copy-Item lang -Destination dist\lang -Recurse
New-Item -ItemType Directory -Force -Path dist\assets | Out-Null
Copy-Item assets\docx-guide -Destination dist\assets\docx-guide -Recurse
Copy-Item src\mcp-server -Destination dist\mcp-server -Recurse
Copy-Item src\office -Destination dist\office -Recurse
```

Verify: `dir dist` should show `ui.exe`, `email-watcher.exe`, `core-cli.exe`,
`lang`, `assets`, `mcp-server`, `office`.

### 4. Create config.json

```powershell
Copy-Item install\config.example.json config.json
notepad config.json
```

Fill in real values: Gmail address + **App Password** (generate one at
myaccount.google.com/apppasswords — not your normal password),
`granted_folders` (real Desktop/Downloads/Documents paths on this machine),
`output_folder`, `obsidian_vault_folder`, `printer_name` (exact Windows
printer name), and at least one API key under `llm_providers`
(OpenRouter/Groq/NIM).

### 5. Run the installer

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\install\install.ps1 -SourceDir .\dist -ConfigPath .\config.json
```

This copies everything to `%LOCALAPPDATA%\TechHelp`, creates the MCP-server
and Office-COM Python venvs, patches `config.json` with resolved Python
paths, registers the 30-minute Task Scheduler job, registers the MCP server
with Claude Desktop, and applies the read/write ACLs for Hermes Agent.

### 6. Manual steps (also printed at the end of install.ps1)

- In Claude Desktop → Settings → Cowork, grant access to Desktop/Downloads/
  Documents only.
- Set the Cowork model to Haiku, extended thinking off.
- Upload `skills/conversion-skill/SKILL.md` and
  `skills/daily-index-skill/SKILL.md` via Customize → Skills.
- Schedule the daily index pass at 9:00 AM via Cowork's `/schedule`.
- Restart Claude Desktop.
- If you ever need to write to a source folder yourself from Explorer and
  can't (because of the ACL applied in step 5), run:
  `icacls <folder> /remove:d %USERNAME% /T`

### 7. Hermes Agent (separate install)

The daily index relies on **Hermes Agent** (Nous Research) — not bundled
in this repo, install it independently. The CLI invocation in
`skills/daily-index-skill/SKILL.md` (`hermes run --scan-paths ...`) is an
assumed shape and may need adjusting once you have the real CLI in front
of you.

### Obsidian (optional, browsing only)

The daily index writes plain Markdown files (with frontmatter and
`[[wikilinks]]`) to `obsidian_vault_folder` — that's just a folder path,
nothing Obsidian-specific about the write step, and `search_file_index`
reads those files directly without needing Obsidian running. Install
Obsidian only if you want to *browse* the notes visually; the system works
identically without it.
