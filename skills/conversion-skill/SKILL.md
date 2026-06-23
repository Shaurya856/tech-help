---
name: conversion-skill
description: Use whenever the user asks to convert, compress, shrink, or "make smaller" a file (image, DOCX, XLSX, or PDF), or to turn a photo/scan into a PDF.
---

# File conversion and compression

When the user asks to convert a file to PDF, or to shrink/compress a PDF
that's too big to email or upload:

1. Locate the file. If the user names it directly, use that path. If they
   describe it instead ("that bill from last month"), call the
   `search_file_index` MCP tool (see daily-index-skill) rather than scanning
   folders yourself.
2. Call the `process_file` MCP tool with the file's path. Do not use
   computer-use or screen-clicking to perform the conversion or compression
   yourself — `process_file` already does this with the same logic used by
   the desktop app and email assistant.
3. Report the result in one direct sentence using the returned
   `output_path` — for example, "Done — saved as bill.pdf in the same
   folder." Do not list multiple options or ask which format they want.

## Reordering pages

If the user asks to reorder, swap, or rearrange pages in a PDF after
conversion, call the `set_page_order` MCP tool with the file path and the
desired 1-based page order. Resolve the intended order from context — do
not ask the user to enumerate page numbers unless the request is genuinely
ambiguous.

## DOCX/XLSX files

Call `process_file` on `.docx`/`.xlsx` files the same as any other
supported type — never attempt to convert them yourself via computer-use.
`process_file` converts them via Word/Excel automation on the machine and
returns the same `{status, output_path}` shape as images and PDFs.

If `process_file` returns `status: "unsupported"` for a `.docx`/`.xlsx`
file specifically, that means Office automation isn't available on this
machine. In that case, fall back to telling the user in one or two short
sentences: open the file in Microsoft Word, then File > Print, choose
"Microsoft Print to PDF" as the printer, then Print and choose where to
save it. Do not use jargon like "export" or "render."

## Other unsupported file types

If `process_file` reports the file type as unsupported and it isn't a
DOCX/XLSX file, tell the user plainly that this file type isn't supported
and ask them to try a photo, image, or PDF instead.
