---
name: conversion-skill
description: Use whenever the user asks to convert, compress, shrink, or "make smaller" a file (image or PDF), or to turn a photo/scan into a PDF.
---

# File conversion and compression

When the user asks to convert an image to PDF, or to shrink/compress a PDF
that's too big to email or upload:

1. Locate the file (ask the user where it is, or use the daily summary index
   — see daily-sync-skill — if they describe it instead of naming it).
2. Call the `process_file` MCP tool with the file's path. Do not use
   computer-use or screen-clicking to perform the conversion or compression
   yourself — `process_file` already does this with the same logic used by
   the desktop app and email assistant.
3. Report the result in one direct sentence using the returned
   `output_path` — for example, "Done — saved as bill.pdf in the same
   folder." Do not list multiple options or ask which format they want.

## DOCX files

Never attempt to convert a `.docx` file yourself, and never call
`process_file` on one — `process_file` will report it as unsupported.
Instead, tell the user in one or two short sentences: open the file in
Microsoft Word, then File > Print, choose "Microsoft Print to PDF" as the
printer, then Print and choose where to save it. Do not use jargon like
"export" or "render."

## Unsupported file types

If `process_file` reports the file type as unsupported and it isn't a
DOCX file, tell the user plainly that this file type isn't supported and
ask them to try a photo, image, or PDF instead.
