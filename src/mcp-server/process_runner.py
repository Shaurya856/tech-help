import json
import os
import subprocess


def process_file_via_cli(cli_path: str, input_path: str) -> dict:
    result = subprocess.run([cli_path, input_path], capture_output=True, text=True)
    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError as e:
        raise RuntimeError(
            f"core-cli produced invalid output: stdout={result.stdout!r} stderr={result.stderr!r}"
        ) from e


def convert_office_to_pdf(python_exe: str, convert_script: str, input_path: str, output_path: str) -> bool:
    """Runs the same Word/Excel COM automation script used by the email
    watcher. Returns True on success, False if not configured or the
    conversion fails/times out — caller should fall back to the manual
    Print-to-PDF guide in that case."""
    if not python_exe or not convert_script:
        return False
    if not os.path.exists(python_exe) or not os.path.exists(convert_script):
        return False
    try:
        result = subprocess.run(
            [python_exe, convert_script, input_path, output_path],
            capture_output=True, text=True, timeout=60,
        )
    except subprocess.TimeoutExpired:
        return False
    return result.returncode == 0 and os.path.exists(output_path)
