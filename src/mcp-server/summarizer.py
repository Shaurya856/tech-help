import os

import requests
from PIL import Image
from pypdf import PdfReader

try:
    import pytesseract
except ImportError:
    pytesseract = None

MAX_CHARS = 8000
REQUEST_TIMEOUT = 30  # seconds per provider attempt

# Provider chain: tried in order. Skipped if api_key is absent or empty.
# Trigger next provider on HTTP 429/5xx or request timeout.
_PROVIDERS = [
    {
        "name": "openrouter",
        "base_url": "https://openrouter.ai/api/v1/chat/completions",
        "model": "meta-llama/llama-3.1-8b-instruct:free",
    },
    {
        "name": "groq",
        "base_url": "https://api.groq.com/openai/v1/chat/completions",
        "model": "llama-3.1-8b-instant",
    },
    {
        "name": "nvidia_nim",
        "base_url": "https://integrate.api.nvidia.com/v1/chat/completions",
        "model": "meta/llama3-8b-instruct",
    },
]


def extract_text(path: str) -> str:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".pdf":
        return _extract_pdf_text(path)
    if ext in (".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff",
               ".heic", ".heif", ".tga", ".gif"):
        return _extract_image_text(path)
    raise ValueError(f"unsupported file type for summarization: {ext}")


def _extract_pdf_text(path: str) -> str:
    reader = PdfReader(path)
    return "\n".join(page.extract_text() or "" for page in reader.pages).strip()


def _extract_image_text(path: str) -> str:
    if pytesseract is None:
        raise RuntimeError(
            "OCR requires the Tesseract engine installed on this machine "
            "(see src/mcp-server/README.md) and the pytesseract package"
        )
    image = Image.open(path)
    return pytesseract.image_to_string(image).strip()


def summarize(text: str, llm_providers: dict) -> str:
    """Try providers in order, fall through on 429/5xx/timeout."""
    if not text.strip():
        return "(no readable text found in this file)"

    prompt = (
        "Summarize this document in 2-3 short sentences, plain language, "
        "no headers or bullet points:\n\n" + text[:MAX_CHARS]
    )

    last_error = "no providers configured with valid API keys"

    for provider in _PROVIDERS:
        name = provider["name"]
        api_key = (llm_providers.get(name) or {}).get("api_key", "")
        if not api_key or api_key.startswith("REPLACE_ME"):
            continue

        try:
            response = requests.post(
                provider["base_url"],
                headers={
                    "Authorization": f"Bearer {api_key}",
                    "Content-Type": "application/json",
                },
                json={
                    "model": provider["model"],
                    "messages": [{"role": "user", "content": prompt}],
                    "temperature": 0.2,
                },
                timeout=REQUEST_TIMEOUT,
            )

            if response.status_code in (429, 500, 502, 503, 504):
                last_error = f"{name} returned {response.status_code}"
                continue

            response.raise_for_status()
            return response.json()["choices"][0]["message"]["content"].strip()

        except (requests.Timeout, requests.ConnectionError) as e:
            last_error = f"{name} timed out or unreachable: {e}"
            continue
        except Exception as e:
            last_error = f"{name} error: {e}"
            continue

    raise RuntimeError(f"all providers failed: {last_error}")
