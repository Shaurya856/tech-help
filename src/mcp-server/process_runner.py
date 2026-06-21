import json
import subprocess


def process_file_via_cli(cli_path: str, input_path: str, threshold_mb: int | None = None) -> dict:
    args = [cli_path, input_path]
    if threshold_mb is not None:
        args.append(str(threshold_mb))

    result = subprocess.run(args, capture_output=True, text=True)
    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError as e:
        raise RuntimeError(
            f"core-cli produced invalid output: stdout={result.stdout!r} stderr={result.stderr!r}"
        ) from e
