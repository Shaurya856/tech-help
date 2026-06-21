# tech-help

A local-first file assistant: converts images to PDF, compresses oversized PDFs,
and routes anything it can't handle (DOCX) to a simple guide — usable from a
desktop app, by email, or via an MCP tool for Claude Cowork.

## Status

CI/build scaffolding only. Core conversion logic, the UI, the email watcher,
and the MCP server are stubs pending implementation.

## Repository layout

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
```

## Build

Builds run on `windows-latest` via GitHub Actions (`.github/workflows/build.yml`)
on every push to `main` and on `v*` tags. No local Windows toolchain or
cross-compilation setup is needed — push, then pull the `.exe` artifacts from
the Actions run (or the GitHub Release, for tagged builds).

To configure locally for development (any platform with CMake + a C++17 compiler):

```
cmake -S . -B build
cmake --build build
```

## Configuration

Copy `install/config.example.json` to `install/config.json` and fill in real
values (Gmail credentials, folder paths, LLM API key). `config.json` is
git-ignored and must be transferred to the target machine separately.
