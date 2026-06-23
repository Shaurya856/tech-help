# mcp-server

Local MCP server exposing four tools to Claude Desktop / Cowork:

- `process_file(path)` — calls the compiled `core-cli` binary (built from `/src/core`), which runs the same image→PDF/compression logic used by the desktop UI and email watcher. No conversion logic is reimplemented here. Does not handle DOCX/XLSX — those convert only via the email watcher's COM subprocess path (see `/src/office`); `process_file` reports them as unsupported.
- `summarize_file(path)` — extracts text (PDF via `pypdf`, images via OCR) and sends it to a free-tier external LLM via a provider chain (OpenRouter → Groq → NVIDIA NIM, configured in `config.json`'s `llm_providers`, falling through on 429/5xx/timeout) for a short summary. For ad-hoc single-file requests outside the daily index — Hermes Agent (see `daily-index-skill`) does its own summarization for the indexing pass and does not call this tool.
- `search_file_index(query)` — fuzzy-matches (`rapidfuzz`, `WRatio`) against the Markdown notes Hermes Agent writes to `obsidian_vault_folder`, returning a ranked `{path, summary, score}` list. Never loads the full index into context.
- `set_page_order(file_path, page_order)` — reorders pages in a PDF in place via `core-cli reorder`, given a 1-based page order list.

## Setup

```
python3 -m venv venv
source venv/bin/activate   # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

Copy `core-cli.exe` (built by CI, see root README) into this directory, or set
`TECH_HELP_CORE_CLI` to its path. Copy `config.json` (see
`install/config.example.json`) into this directory, or set `TECH_HELP_CONFIG`
to its path.

## OCR prerequisite

`summarize_file` uses `pytesseract` for image OCR, which requires the
**Tesseract OCR engine** installed separately on the machine (it's not a
linkable library dependency here, unlike the rest of this project — `pip
install` alone is not enough). On Windows, install it via the official
Tesseract installer and ensure `tesseract.exe` is on `PATH`. If Tesseract
isn't installed, `summarize_file` raises a clear error for image inputs;
PDF text extraction is unaffected.

## Running

```
python3 server.py
```

Registered in Claude Desktop's MCP config by `install/install.ps1`.
