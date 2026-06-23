# office

Word/Excel COM automation, used only by the email watcher's DOCX/XLSX
conversion path — not imported by the MCP server or the UI.

`convert.py` is invoked as a subprocess from C++ (`src/email-watcher/office_convert.cpp`):

```
python convert.py <input.docx|.xlsx> <output.pdf>
```

- `.docx`/`.doc` → `win32com.client.Dispatch("Word.Application")`,
  `ExportAsFixedFormat` (native PDF export, no print-driver dialog).
- `.xlsx`/`.xls` → `win32com.client.Dispatch("Excel.Application")`,
  `ExportAsFixedFormat`.
- Exits 0 on success, 1 on failure. The calling C++ process enforces a
  60-second hang timeout (`WaitForSingleObject` + `TerminateProcess`) —
  this script does not need its own timeout logic.

Requires Microsoft Word/Excel installed on the machine — there is no
COM-free fallback for these two formats (see spec §11).

## Setup

```
python -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
```

`install.ps1` creates this venv and patches `config.json`'s `python_exe`
and `office_convert_script` to point at it.
