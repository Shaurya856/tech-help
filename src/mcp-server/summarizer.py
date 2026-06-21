import os

import requests
from PIL import Image
from pypdf import PdfReader

try:
    import pytesseract
except ImportError:  # pytesseract is optional until OCR is actually needed
    pytesseract = None

MAX_CHARS = 8000

PROVIDER_DEFAULTS = {
    "groq": {
        "base_url": "https://api.groq.com/openai/v1/chat/completions",
        "model": "llama-3.1-8b-instant",
    },
    "nvidia_nim": {
        "base_url": "https://integrate.api.nvidia.com/v1/chat/completions",
        "model": "meta/llama3-8b-instruct",
    },
}


def extract_text(path: str) -> str:
    ext = os.path.splitext(path)[1].lower()
    if ext == ".pdf":
        return _extract_pdf_text(path)
    if ext in (".jpg", ".jpeg", ".png"):
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


def summarize(text: str, llm_provider: dict) -> str:
    if not text.strip():
        return "(no readable text found in this file)"

    provider_name = llm_provider.get("name", "groq")
    defaults = PROVIDER_DEFAULTS.get(provider_name, PROVIDER_DEFAULTS["groq"])
    api_key = llm_provider["api_key"]

    response = requests.post(
        defaults["base_url"],
        headers={"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"},
        json={
            "model": defaults["model"],
            "messages": [
                {
                    "role": "user",
                    "content": (
                        "Summarize this document in 2-3 short sentences, plain language, "
                        "no headers or bullet points:\n\n" + text[:MAX_CHARS]
                    ),
                }
            ],
            "temperature": 0.2,
        },
        timeout=30,
    )
    response.raise_for_status()
    return response.json()["choices"][0]["message"]["content"].strip()
