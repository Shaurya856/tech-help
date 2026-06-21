# Product Specification — File Assistant for Non-Technical User

## Context & Guiding Principle
Built for a non-technical user (the requester's father) who: does not read instructions carefully, forgets file locations, gets defensive about being "taught," cannot verify whether an automated action was correct, and primarily uses email (not new apps) on his phone/laptop comfortably.

**Core principle: automate the grunt work, never the judgment.** Anything mechanical/deterministic (file conversion, compression, search/indexing) is fully automated. Anything with real consequences if wrong (sending something to someone, modifying a shared calendar) requires explicit human action — never automated end-to-end.

## Target Environment
- **End-user machine**: Windows laptop (Intel i5-10210U, 4 cores/8 threads, 16GB RAM, integrated graphics, no dedicated GPU). Resource-conscious design required — avoid heavy background processes.
- **Developer machine**: macOS. Windows binaries are NOT compiled locally — see CI/Build section.
- **Constraint**: "Our code is the only dependency" — no shelling out to external installed applications (e.g., no LibreOffice) for the automated conversion path. DOCX is explicitly excluded from automated conversion (see Scope Exclusions).

---

## Scope: What Gets Built

### 1. Core Conversion/Compression Logic (shared library)
A single core module, written in C++, exposing one primary function:

```
process_file(input_path) -> output_path
```

Behavior:
- Detect file type by extension/content (image: jpg/png/etc., PDF, or unsupported).
- If image: convert to PDF.
- If already PDF: pass through to compression check.
- If resulting/input PDF exceeds a size threshold (default: 5MB, configurable): compress in place by reducing embedded image resolution/quality. Output remains `.pdf` — **never** zip/archive compression.
- If file type is DOCX or otherwise unsupported: return a specific "unsupported — see guide" signal (not a silent failure, not a crash).
- All processing is local; no network calls in this module.

Libraries (no external application dependency):
- Image → PDF: a linkable, free C/C++ library (e.g., direct use of a minimal PDF-writing library such as libharu or PoDoFo) bundled/compiled into the binary.
- PDF compression: linkable library only (e.g., pikepdf-equivalent C/C++ approach, or direct manipulation via a linked PDF library) — no shelling out to Ghostscript as an external process; if a C/C++-linkable Ghostscript library binding is used, it must be statically linked, not invoked as a separate process.

### 2. Standalone Desktop UI
- Single-window application (native Windows, built from the cross-compiled/CI-built binary).
- One primary action: a button or drop-zone labeled to add a file (file picker or drag-and-drop).
- On file selection:
  - If image/PDF: runs `process_file`, shows result screen with output filename/location and a button to open the file or its containing folder.
  - If DOCX: shows a short, static, screenshot-based walkthrough (open in Word → File → Print → "Microsoft Print to PDF" → Save). No automated conversion attempted for DOCX.
- No settings screen, no format-selection step, no second window. One screen, one decision point (pick a file), automatic routing after that.
- Output files saved to one fixed, configurable output folder.

### 3. Email Watcher (background, scheduled)
- A separate small executable, **not a persistent resident process**. Launched on a schedule by Windows Task Scheduler every 30 minutes, runs its check, exits.
- Connects via IMAP to a dedicated Gmail account (credentials in config file, see Configuration section).
- On each run:
  - Checks for new emails with attachments since last check (track last-seen via a local state file, e.g. last UID/timestamp).
  - For each attachment: runs `process_file`.
  - If successful (image/PDF path): sends a reply email (via SMTP) with the processed file attached.
  - If DOCX: sends a reply email with the same screenshot-walkthrough images attached/embedded, and a one-line plain message (in the user's language — see Localization) explaining Word can do this directly.
  - If file type unrecognized entirely: sends a short reply saying the file type isn't supported (no silent drop).
  - **Print trigger**: if the email's subject line contains the word "print" as a whole word, case-insensitive (not a substring match — e.g. "printer" must not false-trigger), after `process_file` completes successfully, the resulting PDF is sent directly to a configured printer via the Windows print spooler API (`winspool.h`), with no application window or UI automation involved — a direct OS-level print job submission, not launching any external viewer/application.
    - Reply email confirms: "Printed!" (or equivalent, localized) on success.
    - On failure (printer offline/unreachable/job rejected), reply explains the specific failure reason in plain language and still attaches the processed PDF as a fallback, so the file isn't lost even if printing didn't work.
    - If the laptop itself is off, no reply is possible at all (the watcher isn't running) — this is a known, undocumented-to-user limitation, not something to attempt to detect or report on.
    - If no "print" subject trigger is present, behavior is unchanged (reply with processed file only, no physical print attempted).
    - Printer name/identifier is read from `config.json` (see Configuration section).
  - **Print trigger**: if the email's subject line contains the word "print" (case-insensitive, exact word match — not substring match, so e.g. "printer" should not false-trigger; configurable trigger word), after `process_file` completes successfully, the resulting PDF is sent directly to a configured printer via the Windows print spooler API (`winspool.h`), with no application window or UI automation involved — a direct OS-level print job submission, not launching any external viewer/application. The reply email confirms the file was sent to the printer (in addition to, or instead of, attaching the file — TBD by Claude Code based on what's cleanest). If no "print" subject trigger is present, behavior is unchanged from the base flow (reply with processed file only, no physical print).
  - Printer name/identifier is read from `config.json` (added to Configuration section below). If the configured printer is unreachable/offline at the time, the watcher should fail gracefully — reply to the email noting the print failed (with the file still attached so it isn't lost), rather than silently dropping the print job.

### 4. MCP Server Wrapper
- Exposes the same core `process_file` function as an MCP tool, so Claude Cowork can call it directly (e.g., after Cowork has located a file via search/judgment, it calls this tool to convert/compress it — no re-implementation of conversion logic inside Cowork's own reasoning).
- Also exposes a `summarize_file(path)` function as a separate MCP tool. This function:
  - Reads file content (text extraction for PDFs/images via OCR if needed — Tesseract OCR via its C++ API).
  - Delegates the actual summarization to **Hermes Agent** (Nous Research, open-source), rather than calling an LLM provider directly. Hermes is installed and run as its own local process; this MCP function calls into Hermes (via Hermes's local API/MCP interface) with the extracted file content and receives a summary back.
  - Hermes is configured with **Groq as primary provider, NVIDIA NIM as fallback** (via Hermes's `fallback_providers` config) — Hermes owns all retry/fallback logic internally; this codebase does not implement its own provider-switching.
  - Returns the summary text returned by Hermes.
  - This remains the only piece of the system that makes external network LLM calls outside of Claude/Cowork itself — now mediated entirely through Hermes rather than hand-written provider calls.
- MCP server is a local stdio/HTTP MCP server (per standard MCP server conventions), registered in Claude Desktop's MCP configuration during install.

### 5. Claude Cowork Configuration
- **Granted folder access**: Desktop, Downloads, Documents only. Not full disk, not Program Files/system folders.
- **Model**: Haiku as default, no extended thinking enabled. (Rationale: fuzzy/ambiguous live file search — e.g., "find that bill from last month" — is explicitly NOT something Cowork does via live reasoning over raw files. See section 6.)
- **Custom instructions** (set via Settings > Cowork > Global instructions): respond in the user's language (see Localization), one instruction/line at a time waiting for confirmation before continuing, no jargon, give one direct answer/file rather than a list of options unless genuinely ambiguous.
- **A Skill** (SKILL.md, uploaded via Customize > Skills) instructing Cowork to:
  - Always prefer calling the `process_file` MCP tool over using computer-use/screen-clicking for any conversion or compression request.
  - Never attempt DOCX conversion — direct the user to the Word-based screenshot guide instead.
  - When asked to find a file, consult the daily-generated summary index (section 6) before attempting a live folder search.

### 6. Daily Scheduled Summary/Index Pass
- A Cowork **scheduled task** (via Cowork's native `/schedule` feature), running once daily at 9:00 AM, only while Claude Desktop is open and the machine is awake.
- Behavior, instructed via the same/companion Skill:
  - Scan the three granted folders for new or changed files since the last run.
  - For each new/changed file, call the `summarize_file` MCP tool to generate a short description.
  - Write/update one markdown note per file into a designated local Obsidian vault folder, in Obsidian-compatible format (YAML frontmatter + content), including file path, type, date, and the generated summary.
  - Add `[[wikilinks]]` between notes that share clear keyword/topic overlap (e.g., same detected biller, same subject) — simple rule-based or LLM-judged linking, not a separate engineering system.
- This index is the mechanism by which "find that bill from last month"-style requests get answered: Cowork (Haiku) matches the request against the pre-written summaries/notes rather than reasoning over raw files cold. This is what makes Haiku-without-extended-thinking sufficient for the system overall.
- Obsidian the application is NOT required to run for this to work — the daily pass writes plain `.md` files to disk. Obsidian only needs to be opened if a human wants to visually browse the graph.

### 7. Scope Exclusions (explicitly NOT built)
- No automated DOCX→PDF conversion (screenshot guide only, in both UI and email reply).
- No automated email-sending of files to third parties (Cowork may find/open/show a file; it does not autonomously send it elsewhere). If this is added later, it requires an explicit "ask before acting" confirmation step — not in scope now.
- No WhatsApp integration (cost/complexity evaluated and declined).
- No voice interface.
- No local LLM on the end-user machine (hardware insufficient; summarization is offloaded to free cloud providers instead).
- No full-disk Cowork access.
- No multi-user/family-wide features (per-person folders, group chat bot, calendar management) — single-user (father) scope only for this build.

---

## Prerequisites (new, due to Hermes dependency)
- **Hermes Agent** must be installed and running on the machine handling the daily summary pass (his laptop, alongside the rest of the system). Configured with:
  - Provider: Groq (primary), NVIDIA NIM (fallback) — both free-tier.
  - No messaging gateway (Telegram/Discord/etc.) needed for this use case — Hermes is used purely as a local summarization engine via its API, not its chat/messaging features.
- This is the one explicit exception to "our code is the only dependency" — scoped deliberately to the `summarize_file` path only, not the core conversion logic.

## Configuration
A single config file (e.g. `config.json`), excluded from git via `.gitignore`, containing:
- Gmail IMAP/SMTP credentials for the dedicated inbox.
- Granted folder paths (Desktop/Downloads/Documents, absolute paths).
- Output folder path for processed files.
- PDF compression size threshold (default 5MB).
- Obsidian vault folder path.
- Free LLM provider API key (Groq or NVIDIA NIM) for the `summarize_file` tool.
- Printer name/identifier (Windows printer name, exact string as registered in Settings > Printers) for the email print-trigger feature.
- User's preferred language code (for reply message text and Skill instructions).

A `config.example.json` template (no real secrets) is committed to the repo so the structure is documented and reproducible on a new machine.

## Installer
A single install script (`install.ps1`, PowerShell, run on the target Windows machine) that:
1. Copies the compiled executables (UI app, email watcher) to a fixed install directory.
2. Prompts for or reads `config.json` (must be supplied alongside the script — not committed to git, copied manually/securely per machine).
3. Creates two Windows Task Scheduler entries:
   - Email watcher: every 30 minutes.
   - (Cowork's daily summary pass is scheduled inside Cowork itself, not via this installer — Task Scheduler only handles the email watcher.)
4. Registers the MCP server in Claude Desktop's local MCP config file.
5. Does NOT require admin rights beyond what Task Scheduler creation needs for a per-user (not system-wide) task.

Running this same script on a new/replacement laptop, with the same `config.json` (paths adjusted if needed), should fully reproduce the setup.

## CI/Build Pipeline
- GitHub repository (new, private), initialized as the first build step.
- `.github/workflows/build.yml`: GitHub Actions workflow, triggers on push to `main`, runs on a `windows-latest` runner, compiles the C++ project (UI app + email watcher), uploads the resulting `.exe` files as build artifacts (and/or attaches to a GitHub Release on tagged commits).
- Developer (Mac-based) workflow going forward: edit code → push → download the freshly built Windows binaries from the Actions run → transfer to target machine (manually, or have the installer auto-fetch the latest release in a future iteration — not required for v1).
- No local cross-compilation toolchain required on the Mac.

## Repository Structure (proposed)
```
/src
  /core          - process_file, summarize_file logic (C++)
  /ui            - standalone desktop UI app
  /email-watcher - scheduled IMAP/SMTP checker
  /mcp-server    - MCP wrapper exposing process_file + summarize_file
/assets
  /docx-guide    - screenshot images for the Word print-to-PDF walkthrough
/install
  install.ps1
  config.example.json
/skills
  conversion-skill/SKILL.md
  daily-sync-skill/SKILL.md
/.github/workflows
  build.yml
README.md
.gitignore        - excludes config.json, build artifacts, local state files
```

## Localization
All user-facing text (UI labels, email reply text, Skill-driven Cowork responses) must support the father's preferred language (to be specified in `config.json`), not hardcoded to English.

## Explicit Non-Goals (for Claude Code to avoid scope creep on)
- Do not build WhatsApp, Telegram, or any messaging-platform integration beyond email.
- Do not build a general-purpose chatbot interface.
- Do not implement DOCX parsing/rendering of any kind.
- Do not add any automated outbound communication to third parties.
- Do not persist or log file contents beyond what's needed for the daily summary note.