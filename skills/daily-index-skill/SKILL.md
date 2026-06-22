---
name: daily-index-skill
description: Runs once daily at 9:00 AM via the Cowork schedule to index files into Obsidian notes using Hermes Agent, and is also consulted whenever the user asks to find a file by description rather than by name.
---

# Daily file index

## Scheduled pass (9:00 AM daily)

This pass is driven by **Hermes Agent** (Nous Research), a local agent that
handles file discovery, text extraction, summarisation, and note writing.
Cowork's role is solely to invoke it and report the outcome.

### Invocation

Run Hermes Agent via its CLI, passing the three source folders and the index
output directory from `config.json`:

```
hermes run \
  --scan-paths "<Documents>,<Downloads>,<Desktop>" \
  --output-dir "%LOCALAPPDATA%\FatherTechAssist\index" \
  --provider openrouter \
  --fallback-providers groq,nvidia_nim \
  --api-keys openrouter=<key>,groq=<key>,nvidia_nim=<key>
```

Hermes exits on completion — it is not a standing process. Wait for it to
finish before reporting success or failure to the user.

### What Hermes does (do not replicate manually)

1. **Diffing**: compares file mtimes and content hashes against the previous
   run's state. Classifies each file as added / modified / unchanged / removed.
2. **Text extraction**: for native-text PDFs, extracts text directly.
   For image-based files (scanned PDFs, photos), runs Tesseract OCR first.
3. **Summarisation**: for added/modified files, calls a backend model via the
   provider chain (OpenRouter → Groq → NIM, trigger on 429/5xx/30 s timeout).
4. **Note writing**: one Markdown note per file in the index output folder,
   with YAML frontmatter:
   ```yaml
   ---
   source_path: <absolute path to original file>
   content_hash: <SHA-256 of file contents>
   last_indexed: <ISO 8601 timestamp>
   ---
   ```
   And `[[wikilinks]]` between notes whose summaries share a real content
   relationship (same biller, sender, topic) — not just filename similarity.
5. **Removed files**: corresponding notes are flagged with a `removed: true`
   frontmatter key and archived to a `_removed/` subfolder, not deleted, so
   no dead links remain.
6. **Partial-failure recovery**: Hermes checkpoints per file. A crashed run
   resumes from where it left off on the next invocation, not from scratch.

### Read/write boundary — OS-enforced, not just documented

- Hermes Agent has **read access only** to Documents, Downloads, and Desktop.
  This is enforced at the OS ACL level (set by `install.ps1`), not solely by
  this instruction.
- Hermes Agent has **write access only** to `%LOCALAPPDATA%\FatherTechAssist\index\`.
  It cannot write anywhere else — attempting to do so will fail at the OS level.
- This SKILL.md is a second layer of documentation, not the only guardrail.

## Answering "find that file" requests

Use the `search_file_index` MCP tool — it performs a lexical/fuzzy match
against the note titles, frontmatter `source_path`, and summary text using
rapidfuzz. It returns a ranked list of `{path, summary}` pairs.

- Give one direct answer (the file path) rather than a list of candidates,
  unless the request is genuinely ambiguous between two or more files.
- If `search_file_index` returns an empty list, say "couldn't find it in the
  index" directly — do not fall back to a live folder scan.
- Never load the full index graph into context; always use `search_file_index`.
