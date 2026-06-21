import os

from mcp.server.fastmcp import FastMCP

from config import load_config
from process_runner import process_file_via_cli
from summarizer import extract_text, summarize

mcp = FastMCP("tech-help")

_config = load_config()
_CORE_CLI_PATH = os.environ.get(
    "TECH_HELP_CORE_CLI",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "core-cli.exe"),
)


@mcp.tool()
def process_file(path: str) -> dict:
    """Convert an image to PDF, or compress a PDF, in place. Returns
    {status, output_path}. status is 'ok', 'unsupported' (e.g. docx), or
    'error'. Always use this instead of computer-use/screen-clicking for
    conversion or compression requests."""
    return process_file_via_cli(_CORE_CLI_PATH, path)


@mcp.tool()
def summarize_file(path: str) -> str:
    """Generate a short plain-language summary of a PDF or image's content,
    using a free-tier external LLM so Claude's own tokens aren't spent on
    routine summarization."""
    text = extract_text(path)
    return summarize(text, _config["llm_provider"])


if __name__ == "__main__":
    mcp.run()
