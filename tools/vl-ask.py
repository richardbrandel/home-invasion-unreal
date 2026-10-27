#!/usr/bin/env python3
"""Ask a vision model about a screenshot, through a request shape that is KNOWN to work.

WHY THIS EXISTS
    Richard's Qwen 3 VL Plus client started returning "This turn failed400" on every turn. The
    model, the key and the image were all fine -- measured 2026-10-26, this exact request returns
    HTTP 200 and a usable critique of kitchen.png:

        STATUS 200
        1. Flat, textureless walls and surfaces  2. Overly uniform, harsh lighting ...

    So the 400 lives in whatever client is producing it, not in the model. Rather than debug a
    black box mid-loop, this is a minimal, inspectable client: it builds the OpenAI-compatible
    body itself, prints the HTTP status, and prints the error BODY on failure instead of a status
    code alone. A 400 with no body is the reason this class of failure costs days.

USAGE
    python3 tools/vl-ask.py kitchen.png "List the three most obvious visual problems."
    python3 tools/vl-ask.py --prompt "..." --images front.png kitchen.png

NOTES
    - The image is resized to 640x360 before sending: the scene is judged as a whole, a full
      1280x720 frame costs more tokens for no more information, and the resize is what the
      working call above used.
    - The key is read from ~/.dsh/.credentials.yaml by NAME and never printed or logged.
    - Endpoint and model are the ones Richard's workspace serves (`qwen3-vl-plus` was confirmed
      present in GET /models -- 61 models -- so an unavailable-model 400 is ruled out).
"""

import argparse
import re
import base64
import io
import json
import pathlib
import sys
import urllib.error
import urllib.request

URL = ("https://ws-k63u11uw1y8ut81o.us-east-1.maas.aliyuncs.com"
       "/compatible-mode/v1/chat/completions")
MODEL = "qwen3-vl-plus"
CREDS = pathlib.Path.home() / ".dsh" / ".credentials.yaml"


def api_key():
    for line in CREDS.read_text().splitlines():
        s = line.strip()
        if s.startswith("QWEN_US_API_KEY:"):
            return s.split(":", 1)[1].strip().strip('"\'')
    raise SystemExit("vl-ask: QWEN_US_API_KEY not found in ~/.dsh/.credentials.yaml")


def encode(path):
    from PIL import Image
    img = Image.open(path).convert("RGB").resize((640, 360), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return base64.b64encode(buf.getvalue()).decode()


def ask(prompt, images, max_tokens=800):
    content = [{"type": "text", "text": prompt}]
    for p in images:
        content.append({"type": "image_url",
                        "image_url": {"url": f"data:image/png;base64,{encode(p)}"}})
    body = {"model": MODEL, "messages": [{"role": "user", "content": content}],
            "max_tokens": max_tokens}
    req = urllib.request.Request(
        URL, data=json.dumps(body).encode(),
        headers={"Authorization": f"Bearer {api_key()}",
                 "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            out = json.load(r)
        return out["choices"][0]["message"]["content"]
    except urllib.error.HTTPError as e:
        # THE BODY IS THE POINT. A bare "400" tells you nothing; the body names the field.
        print(f"vl-ask: HTTP {e.code}", file=sys.stderr)
        print("vl-ask: body -> " + e.read().decode()[:800], file=sys.stderr)
        return None
    except Exception as e:
        print(f"vl-ask: {type(e).__name__}: {e}", file=sys.stderr)
        return None


def ask_json(prompt, images, max_tokens, attempts=3):
    """Ask for a JSON object and return it parsed, retrying on non-JSON.

    400 TOKENS IS NOT ENOUGH for a diagnosis plus several before/after fixes, which is why the
    Master's proxy kept logging VL_ERROR while the model was answering fine: it asked for JSON in
    400 tokens and then json.loads() on a truncated string. 1600 here, and a retry that says what
    was wrong, rather than failing the turn.
    """
    q = prompt
    for attempt in range(attempts):
        answer = ask(q, images, max_tokens)
        if answer is None:
            return None
        m = (re.search(r"```(?:json)?\s*(\{.*?\})\s*```", answer, re.S)
             or re.search(r"(\{.*\})", answer, re.S))
        if m:
            try:
                return json.loads(m.group(1))
            except json.JSONDecodeError as e:
                print(f"vl-ask: attempt {attempt+1}: invalid JSON ({e})", file=sys.stderr)
        q = prompt + ("\n\nYour previous reply could not be parsed as JSON. "
                      "Output ONLY the JSON object, no prose, no code fence.")
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", help="screenshot to analyse")
    ap.add_argument("question", nargs="?", help="what to ask about it")
    ap.add_argument("--prompt", default=None, help="the question, if you prefer a flag")
    ap.add_argument("--images", nargs="*", default=None, help="several images at once")
    ap.add_argument("--max-tokens", type=int, default=800)
    ap.add_argument("--json", action="store_true", help="require a JSON object; retry if not")
    a = ap.parse_args()

    images = a.images or ([a.image] if a.image else [])
    prompt = a.prompt or a.question or "Describe the visual problems in this screenshot."
    if not images:
        print("vl-ask: no image given", file=sys.stderr)
        return 2
    for img in images:
        if not pathlib.Path(img).exists():
            print(f"vl-ask: no such image {img}", file=sys.stderr)
            return 2
    if a.json:
        obj = ask_json(prompt, images, max(a.max_tokens, 1600))
        if obj is None:
            return 1
        print(json.dumps(obj, indent=2))
        return 0
    answer = ask(prompt, images, a.max_tokens)
    if answer is None:
        return 1
    print(answer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
