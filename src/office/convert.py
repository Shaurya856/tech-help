#!/usr/bin/env python3
"""Converts DOCX/XLSX to PDF via Word/Excel COM automation.

Usage:
    python convert.py <input_path> <output_path>

Exit code 0 on success, 1 on failure. Errors written to stderr.
Requires Microsoft Office installed on the machine.
"""

import os
import sys


def convert_docx(input_path: str, output_path: str) -> None:
    import win32com.client  # pywin32

    word = win32com.client.Dispatch("Word.Application")
    word.Visible = False
    doc = None
    try:
        doc = word.Documents.Open(input_path)
        # ExportFormat 17 = wdExportFormatPDF
        doc.ExportAsFixedFormat(output_path, ExportFormat=17)
    finally:
        if doc is not None:
            doc.Close(False)
        word.Quit()


def convert_xlsx(input_path: str, output_path: str) -> None:
    import win32com.client  # pywin32

    excel = win32com.client.Dispatch("Excel.Application")
    excel.Visible = False
    wb = None
    try:
        wb = excel.Workbooks.Open(input_path)
        # Type 0 = xlTypePDF
        wb.ExportAsFixedFormat(0, output_path)
    finally:
        if wb is not None:
            wb.Close(False)
        excel.Quit()


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: convert.py <input_path> <output_path>", file=sys.stderr)
        sys.exit(1)

    input_path = os.path.abspath(sys.argv[1])
    output_path = os.path.abspath(sys.argv[2])
    ext = os.path.splitext(input_path)[1].lower()

    if not os.path.exists(input_path):
        print(f"input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    try:
        if ext in (".docx", ".doc"):
            convert_docx(input_path, output_path)
        elif ext in (".xlsx", ".xls"):
            convert_xlsx(input_path, output_path)
        else:
            print(f"unsupported extension: {ext}", file=sys.stderr)
            sys.exit(1)
    except Exception as e:
        print(f"conversion failed: {e}", file=sys.stderr)
        sys.exit(1)

    if not os.path.exists(output_path):
        print("conversion produced no output file", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
