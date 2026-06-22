# Father's Tech-Assist System — Final Specification

## Core Principle
Automate the grunt work, never the judgment. Dad doesn't read instructions,
forgets file locations, gets confused by choices, won't maintain an
organizational system, and gets defensive about being "taught." He lives in
email. Every primary-path interaction requires zero decisions from him.

---

## 1. Primary Interface — Email Watcher

- IMAP polling every 30 minutes (Gmail), using an **App Password** (single
  credential, used for both IMAP read and SMTP reply).
- **IMAP/SMTP library**: `libcurl` — supports both IMAP and SMTP directly,
  one library for both directions, statically linked.
- Processing is **strictly sequential** — one email, one job, one reply at
  a time. No concurrency anywhere in this path.
- **No attachment found**: reply — "No valid file found in your email.
  Please attach a file to convert."
- **Unsupported file type**: reply — "This file type isn't supported."
- **Output filename**: if subject contains `name: <text>`, use that text
  (sanitized for filesystem-safe characters) as the output filename.
  Otherwise: original filename + timestamp.
- **Output folder**: all converted/processed files saved to one fixed,
  configurable output folder set in `config.json`.
- **De-duplication**: processed email Message-IDs recorded in local state
  (§9) — prevents reprocessing on watcher restart or overlapping poll cycles.

### Supported file types (closed list)
- **PDF** — pass-through, compression only
- **DOCX** (Word) — via Word COM automation
- **XLSX** (Excel) — via Excel COM automation, assumed properly formatted
- **Images**: JPEG, PNG, HEIC, TIFF, BMP, WEBP, TGA, GIF

Anything outside this list → "this file type isn't supported" reply.

### File-type detection
Extension-based (not magic-byte sniffing) — sufficient given the small,
known closed list of formats.

---

## 2. Print-via-Email

- Trigger: exact-word "print" in subject line (not "printer").
- Applies to all four supported file types.
- **Confirmation flow**:
  1. File converted/rendered as normal.
  2. System replies with a first-page preview and asks: "Reply YES to print,
     or reply with changes."
  3. Pending job tracked via unique key, matched against his reply using
     email threading (`In-Reply-To`/`References` headers, falling back to
     subject-line match).
  4. On YES → sent to configured printer via Windows print spooler API.
     Reply: "Printed!" On spooler failure: reply with PDF attached as
     fallback.
  5. **Timeout**: no reply within 24 hours → job discarded. Reply sent:
     "This print request expired — resend if you still need it printed."
- Pending-job tracking persists in local state (§9), not in-memory.

---

## 3. Secondary Interface — Standalone Windows Desktop UI

- **Design**: simple, clean, functional — not flashy. One button/drop-zone
  for the primary conversion flow. No settings screen, no second window, no
  format-selection step. One screen, one decision (pick a file), automatic
  routing after that.
- **Manual reorder screen**: shown for multi-file conversions — thumbnails
  in original attachment/selection order, drag-and-drop to rearrange before
  final PDF assembly.
- File-picker for direct manual conversion.
- **Thumbnail library**: Pillow with `pillow-heif` plugin for HEIC preview
  support — consistent with the Python orchestration layer.

---

## 4. MCP Server (Cowork Integration)

- **Transport: stdio.** Cowork spawns the server as a subprocess per call,
  process exits after responding. No standing service, no port, no idle
  memory. Single local user — no auth, no multi-client handling.
- **Principle**: every system function is exposed identically through the
  Desktop UI and MCP server. Email is the narrower surface — conversion and
  print only, no reordering.
- **Exposed tools**:
  - `process_file(path, file_type)` — same conversion core as UI/email
  - `search_file_index(query)` — returns ranked matches (path + summary),
    never the full graph
  - `set_page_order(file_id, ordered_list)` — manual reorder, Cowork
    resolves the intended order from context and calls this tool directly

---

## 5. Language/Runtime Split

### C++ (email watcher + core)
Handles all conversion, compression, and email I/O — compiled, no
runtime dependency on the target machine.

**Libraries**:
- `libharu` — PDF generation from images
- `QPDF` — structural PDF manipulation, embedded image recompression
  (C++ library directly, not CLI)
- `libheif` + `libde265` — HEIC decode (HEVC codec layer)
- `stb_image` — JPEG/PNG/WEBP/TGA/GIF/BMP decode; `libtiff` for TIFF
  decode (stb_image TIFF support is limited)
- `Tesseract OCR` — via C++ API (not CLI), for text extraction from
  image-based PDFs and photos before summarization
- `nlohmann/json` — header-only JSON parsing for `config.json`
- `libcurl` — IMAP + SMTP for the email watcher
- `SQLite3` — local state: processed Message-IDs, pending print jobs,
  last-seen UID

### Python (3.12)
MCP server, Hermes Agent invocation, COM automation for Word/Excel.
The email watcher and UI remain C++ — Python is for orchestration
layers that benefit from scripting (COM, Hermes CLI calls, MCP).

- **C++ → Python bridge**: subprocess (`core-cli.exe`) — the MCP server
  calls the compiled CLI tool per file. Simpler than pybind11 for the
  current scope; revisit if per-file subprocess overhead becomes
  measurable.
- **Dependency management**: `requirements.txt` with pinned versions.
- **Bundling**: official embeddable Windows Python package (not full
  installer) — minimal footprint, no PATH/registry pollution.
- No idle background processes beyond the email watcher's 30-min poll.

---

## 6. File Conversion Pipeline

### Image → PDF
Built directly via C++ core (libharu). HEIC decoded via libheif + libde265.
stb_image decodes JPEG/PNG/TIFF/BMP into raw pixel data for libharu.

### DOCX → PDF
- Word COM automation: `win32com.client.Dispatch("Word.Application")`,
  `Visible = False`.
- `doc.ExportAsFixedFormat(output_path, ExportFormat=17)` — native PDF
  export, no print-driver intermediary, no dialogs.
- **Hang protection**: 60-second timeout. If Word doesn't return, process
  force-killed, failure reply sent — watcher never blocked indefinitely.
- Serialized by design (§1 sequential processing) — no concurrent COM
  instances.

### XLSX → PDF
- Same pattern: `win32com.client.Dispatch("Excel.Application")`,
  `Visible = False`, native PDF export.
- Assumed properly formatted — no column-width/print-area correction.

### Compression — always on, every file, no size threshold
- Images → PDF: downscaled to ≤1600px long edge, JPEG quality 70.
- Any PDF (direct, or produced from DOCX/XLSX): walk embedded JPEGs
  (`DCTDecode`) via QPDF, recompress in place.
- Target: ~85-90% size reduction on phone camera photos.

### Multi-file ordering
- **Default**: original attachment/selection order — no automatic inference
  (no narration parsing, no filename pattern matching, no EXIF reading).
- **Correction**: manual only, via Desktop UI drag-drop or MCP
  `set_page_order` — never automatic, never inferred from email text.
- **Mixed file types in one email**: no fixed type-based precedence —
  original attachment order applies uniformly.

### Failure handling
- **Corrupted/unreadable file**: reply — "This file couldn't be opened.
  Please check it and resend."
- **Max size**: files over 100MB or more than 30 attachments per email →
  reply — "This is too large to process. Try sending fewer or smaller
  files." (Ceiling adjustable in `config.json`.)

---

## 7. Daily Index Generation (Obsidian Markdown Sync)

### Agent
- **Hermes Agent** (Nous Research's open-source local agent framework),
  chosen for its ready-made local file search capability — not for any
  specific backend model.
- Runs locally, invoked **on-demand only** by Cowork's 9am scheduled job.
  No standing process — exits on completion.
- **Three source folders**: Documents, Downloads, Desktop — standard Windows
  user profile folders, actual paths resolved per-machine and stored in
  `config.json`.

### Read/write boundary — OS-enforced, not just documented
- Agent process: **read access** to three source folders only.
- **Write access**: limited to the dedicated index output folder
  (`%LOCALAPPDATA%\FatherTechAssist\index\`) at the OS permission level
  (ACL) — not just a SKILL.md instruction.
- SKILL.md states this explicitly as a second layer, not the only layer.

### Trigger & invocation
- Cowork's 9am job invokes Hermes Agent via its CLI interface, passing scan
  paths and output path as arguments/config. Process exits on completion.

### Diffing
- Uses Hermes Agent's built-in file search to identify added/modified files
  since prior run (added / modified / unchanged / removed).
- Removed files: corresponding notes flagged/archived, not left as dead
  links.
- **Partial-failure recovery**: progress checkpointed per-file — a crashed
  run resumes from where it left off on the next invocation, not from
  scratch.

### Summarization
- For added/modified files, Hermes Agent calls a backend model for text
  generation.
- **Text extraction**: for image-based files (scanned PDFs, photos of
  documents), Tesseract OCR feeds readable text to the agent before
  summarization. For native-text PDFs, text is extracted directly.
- **Provider chain**: OpenRouter (free tier) → Groq → NIM, triggered by
  HTTP 429/5xx or 30-second timeout per call. Model identity not fixed.
- Agent reads file content to produce connected, cross-referenced
  summaries — not flat per-file descriptions.

### Output
- Markdown notes with `[[wikilinks]]` based on real content relationships
  (shared topic, sender, subject) — renders as Obsidian-native graph.
- **Frontmatter per note**: `source_path`, `content_hash`, `last_indexed`.

### SKILL.md (daily-index-skill)
Must explicitly state:
- Read file content (including OCR output for image-based files) to
  generate connected summaries
- Build wikilinks from real content relationships
- **Strictly read-only on Documents/Downloads/Desktop** — write access
  exists only for the index output folder, enforced at OS level

---

## 8. Retrieval

- `search_file_index` (MCP tool, §4) is the only path Cowork uses to
  resolve file-location queries.
- **Matching**: lexical/fuzzy match against note titles, frontmatter, and
  summary text (`rapidfuzz` or equivalent) — not vector/embedding search.
  Index size and query volume are small enough that this is sufficient and
  keeps the dependency footprint light.
- Returns ranked matches (path + summary snippet) only — never the full
  graph loaded into context.
- **Zero matches**: explicit empty result returned — Cowork tells the user
  "couldn't find it" directly, no guessing.

---

## 9. Local State & Logging

- **SQLite database** (local, single-file): tracks processed email
  Message-IDs (de-duplication), pending print-job confirmations with
  timestamps (24-hour timeout), and the daily index's last-run diff state.
- **Logging**: structured rotating log file at
  `%LOCALAPPDATA%\FatherTechAssist\logs\` — errors and key processing
  events only, no file contents. Primary remote-debugging mechanism: ask
  dad to "send the log file" if something goes wrong.

---

## 10. Packaging & Deployment

### Config (`config.json`, gitignored)
Email App Password, printer name, three source folder paths, output folder
path, dad's preferred language, API keys (OpenRouter/Groq/NIM), size/
attachment limits.
- Stored plaintext locally — accepted risk for single-user local deployment.
- `config.example.json` committed as a template.

### Localization
All user-facing text — UI labels, email replies, Cowork/Skill responses —
rendered in dad's preferred language (set in `config.json`), not hardcoded
to English.

### Installer (`install.ps1`)
1. Copies compiled executables (UI, email watcher, MCP server) to fixed
   install directory.
2. Reads `config.json` (supplied alongside script, not committed).
3. Creates Windows Task Scheduler entry for email watcher (every 30 min,
   per-user, no admin rights).
4. Registers MCP server in Claude Desktop's local MCP config.
5. Sets OS-level ACLs: Hermes Agent process read-only on source folders,
   write-only on index output folder.
6. Re-running this script on a replacement laptop (same `config.json`,
   paths adjusted) fully reproduces the setup.

### Updates
Re-run `install.ps1` with freshly downloaded binaries — no auto-update
mechanism for v1.

### CI/Build
- Private GitHub repo.
- `.github/workflows/build.yml`: triggers on push to `main`, runs on
  `windows-latest`, compiles C++ core + UI + email watcher (no pybind11 —
  Python calls core via subprocess through `core-cli.exe`), uploads `.exe`/build
  artifacts (and/or attaches to tagged
  Releases).
- Developer workflow (Mac-based): edit → push → download built Windows
  binaries from Actions → transfer to target machine. No local
  cross-compile toolchain needed.

### Repository structure

/src
  /core          - C++ conversion/compression (libharu, QPDF, libheif,
                   libde265, stb_image, libtiff, nlohmann/json, SQLite3)
  /ui            - Desktop UI app (C++ Win32)
  /email-watcher - IMAP/SMTP scheduled checker (C++, libcurl, SQLite3)
  /mcp-server    - MCP tools: process_file, search_file_index,
                   set_page_order (Python, stdio transport, core-cli subprocess)
  /office        - Word/Excel COM automation wrappers (Python)
  /ordering      - Multi-file ordering logic (Python)
/install
  install.ps1
  config.example.json
/skills
  daily-index-skill/SKILL.md
/.github/workflows
  build.yml
README.md
.gitignore       - excludes config.json, build artifacts, local state/logs


---

## 11. Explicit Non-Goals
- No WhatsApp, Telegram, or messaging-platform integration beyond email.
- No general-purpose chatbot interface.
- No automated outbound communication to third parties.
- No persisting or logging file contents (logs contain events/errors only).
- No automatic ordering inference — no narration parsing, no filename
  pattern matching, no EXIF reading. Attachment order is default; correction
  is always manual (UI or MCP only).
- No file types beyond the closed list in §1.
- No auto-update mechanism for v1.
- No vector/embedding-based search.
- No DOCX/XLSX rendering without Microsoft Office installed — Word and Excel
  are hard dependencies for those file types specifically.