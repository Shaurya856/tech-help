---
name: daily-sync-skill
description: Runs once daily at 9:00 AM via the Cowork schedule to index files into Obsidian notes, and is also consulted whenever the user asks to find a file by description rather than by name.
---

# Daily file index

## Scheduled pass (9:00 AM daily)

1. Scan the granted folders (Desktop, Downloads, Documents) for files that
   are new or modified since the last run.
2. For each new or changed file, call the `summarize_file` MCP tool to get a
   short content summary. Skip files that fail to summarize rather than
   stopping the whole pass.
3. Write or update one markdown note per file in the configured Obsidian
   vault folder, with YAML frontmatter:

   ```yaml
   ---
   path: <absolute file path>
   type: <file extension>
   date: <file modified date, ISO 8601>
   ---

   <the summary text from summarize_file>
   ```

4. Add `[[wikilinks]]` between notes whose summaries clearly share a topic
   or biller/sender (e.g. two notes both mentioning the same company name).
   Use simple keyword overlap — this does not need to be exact.
5. This pass only runs while Claude Desktop is open. If the machine was
   asleep or Desktop was closed at 9:00 AM, it simply runs at the next
   opportunity — do not attempt to catch up multiple missed days at once
   beyond scanning whatever has changed since the last successful run.

## Answering "find that file" requests

When the user describes a file instead of naming it (e.g. "find that bill
from last month"), search the notes already written to the Obsidian vault
folder for matching summaries first. Only fall back to a live folder search
if nothing in the index matches — this keeps the request fast and within
scope for a model with no extended thinking enabled.

Give one direct answer (the file path) rather than a list of candidates,
unless the request is genuinely ambiguous between two or more files.
