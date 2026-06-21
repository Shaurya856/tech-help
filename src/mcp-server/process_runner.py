import json
import subprocess


def process_file_via_cli(cli_path: str, input_path: str) -> dict:
    result = subprocess.run([cli_path, input_path], capture_output=True, text=True)
    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError as e:
        raise RuntimeError(
            f"core-cli produced invalid output: stdout={result.stdout!r} stderr={result.stderr!r}"
        ) from e
