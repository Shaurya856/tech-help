import json
import os

_DEFAULT_CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")


def load_config(path: str | None = None) -> dict:
    config_path = path or os.environ.get("TECH_HELP_CONFIG", _DEFAULT_CONFIG_PATH)
    with open(config_path, "r", encoding="utf-8") as f:
        return json.load(f)
