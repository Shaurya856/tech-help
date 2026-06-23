import os
import re
import subprocess
import tempfile

from mcp.server.fastmcp import FastMCP

from config import load_config
from process_runner import convert_office_to_pdf, process_file_via_cli
from summarizer import extract_text, summarize

mcp = FastMCP("tech-help")

_config = load_config()
_CORE_CLI_PATH = os.environ.get(
    "TECH_HELP_CORE_CLI",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "core-cli.exe"),
)
_INDEX_DIR = _config.get("obsidian_vault_folder", "")


# ── Helpers ────────────────────────────────────────────────────────────────

def _read_index_notes() -> list[dict]:
    """Load all .md notes from the index directory into a list of dicts
    with keys: path, title, source_path, content_hash, body."""
    if not _INDEX_DIR or not os.path.isdir(_INDEX_DIR):
        return []
    notes = []
    for fname in os.listdir(_INDEX_DIR):
        if not fname.endswith(".md"):
            continue
        fpath = os.path.join(_INDEX_DIR, fname)
        try:
            with open(fpath, encoding="utf-8") as f:
                raw = f.read()
        except OSError:
            continue

        # Parse YAML frontmatter between --- delimiters.
        fm: dict = {}
        body = raw
        if raw.startswith("---"):
            end = raw.find("\n---", 3)
            if end != -1:
                fm_block = raw[3:end]
                for line in fm_block.splitlines():
                    if ":" in line:
                        k, _, v = line.partition(":")
                        fm[k.strip()] = v.strip()
                body = raw[end + 4:].strip()

        notes.append(
            {
                "path": fm.get("source_path", ""),
                "title": os.path.splitext(fname)[0],
                "content_hash": fm.get("content_hash", ""),
                "body": body,
                "note_path": fpath,
            }
        )
    return notes


# ── Tools ──────────────────────────────────────────────────────────────────

_OFFICE_EXTS = (".docx", ".doc", ".xlsx", ".xls")


@mcp.tool()
def process_file(path: str) -> dict:
    """Convert an image, DOCX, or XLSX to PDF, or compress a PDF, in place.
    Returns {status, output_path}. status is 'ok', 'unsupported', or 'error'.
    'unsupported' for a DOCX/XLSX means Office automation isn't available on
    this machine — fall back to the manual Print-to-PDF guide in that case.
    Always use this instead of computer-use for conversion or compression."""
    ext = os.path.splitext(path)[1].lower()
    if ext not in _OFFICE_EXTS:
        return process_file_via_cli(_CORE_CLI_PATH, path)

    python_exe = _config.get("python_exe", "")
    convert_script = _config.get("office_convert_script", "")
    with tempfile.TemporaryDirectory() as tmp_dir:
        office_out = os.path.join(tmp_dir, os.path.splitext(os.path.basename(path))[0] + ".pdf")
        if not convert_office_to_pdf(python_exe, convert_script, path, office_out):
            return {"status": "unsupported"}
        return process_file_via_cli(_CORE_CLI_PATH, office_out)


@mcp.tool()
def summarize_file(path: str) -> str:
    """Generate a short plain-language summary of a PDF or image's content
    using a free-tier external LLM (no Claude tokens spent on summarization)."""
    text = extract_text(path)
    providers = _config.get("llm_providers", {})
    return summarize(text, providers)


@mcp.tool()
def search_file_index(query: str) -> list:
    """Search the local file index for files matching a description.
    Returns a ranked list of {path, summary} dicts (best match first),
    or an empty list if nothing matches. Never loads the full index into context."""
    try:
        from rapidfuzz import fuzz, process as rf_process
    except ImportError:
        return [{"error": "rapidfuzz not installed — run pip install rapidfuzz"}]

    notes = _read_index_notes()
    if not notes:
        return []

    # Build searchable strings: title + source path basename + first 200 chars of body.
    candidates = []
    for note in notes:
        searchable = " ".join([
            note["title"],
            os.path.basename(note["path"]),
            note["body"][:200],
        ])
        candidates.append(searchable)

    results = rf_process.extract(
        query, candidates, scorer=fuzz.WRatio, limit=5, score_cutoff=40
    )

    ranked = []
    for _match_str, score, idx in results:
        note = notes[idx]
        # Return up to 300 chars of body as the summary snippet.
        snippet = note["body"][:300].replace("\n", " ").strip()
        ranked.append({"path": note["path"], "summary": snippet, "score": score})

    return ranked


@mcp.tool()
def set_page_order(file_path: str, page_order: list) -> dict:
    """Reorder pages in a PDF. page_order is a 1-based list of page numbers
    in the desired output order (e.g. [2,1,3] swaps the first two pages).
    The original file is replaced. Returns {status: 'ok'} or {status: 'error', message}."""
    if not os.path.exists(file_path):
        return {"status": "error", "message": f"file not found: {file_path}"}

    order_str = ",".join(str(p) for p in page_order)
    out_path = file_path + ".reordered.pdf"

    try:
        result = subprocess.run(
            [_CORE_CLI_PATH, "reorder", file_path, out_path, order_str],
            capture_output=True, text=True, timeout=60,
        )
    except subprocess.TimeoutExpired:
        return {"status": "error", "message": "reorder timed out"}

    if result.returncode != 0:
        return {"status": "error", "message": result.stdout.strip() or "reorder failed"}

    # Replace original with reordered.
    try:
        os.replace(out_path, file_path)
    except OSError as e:
        return {"status": "error", "message": f"could not replace original: {e}"}

    return {"status": "ok", "output_path": file_path}


if __name__ == "__main__":
    mcp.run()
