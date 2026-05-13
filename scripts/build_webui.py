import os
import gzip
import hashlib
import re

Import("env")

# ── Paths ─────────────────────────────────────────────────────────────────────
PROJECT_DIR   = env.get("PROJECT_DIR")
WEB_APP_DIR   = os.path.join(PROJECT_DIR, "dsp-control-app")
INCLUDE_DIR   = os.path.join(PROJECT_DIR, "include")
OUTPUT_HEADER = os.path.join(INCLUDE_DIR, "embedded_ui.h")
HASH_FILE     = os.path.join(PROJECT_DIR, ".last_web_ui_hash")

# ── Helpers ───────────────────────────────────────────────────────────────────

def _find_ui_file():
    candidates = [
        os.path.join(WEB_APP_DIR, "dist", "index.html"),
        os.path.join(WEB_APP_DIR, "index.html"),
    ]
    for p in candidates:
        if os.path.exists(p):
            return p
    return None


def _strip_query(path: str) -> str:
    return path.split("?")[0]


def _resolve_local(path: str, *search_dirs):
    clean = _strip_query(path).lstrip("/")
    for base in search_dirs:
        full = os.path.join(base, clean)
        if os.path.exists(full):
            return full
    return None


# ── Regex patterns ────────────────────────────────────────────────────────────

# Match any import/export-from statement (to find dependencies)
_IMPORT_RE = re.compile(
    r"""^[ \t]*(?:import|export)(?:\s+[\w*{},\s]+\s+from)?\s+(['"])([^'"]+)\1[^\n]*$""",
    re.MULTILINE
)

# Match full import/re-export lines to DELETE them from output
_IMPORT_STMT_RE = re.compile(
    r"""^[ \t]*(?:import|export\s*(?:\*|\{[^}]*\}))\s*(?:[\w*{},\s]+\s+from\s+)?(['"])[^'"]+\1[^\n]*\n?""",
    re.MULTILINE
)

_EXPORT_DEFAULT_RE = re.compile(r'\bexport\s+default\s+')
_EXPORT_NAMED_RE   = re.compile(r'\bexport\s+(?=(?:const|let|var|function|async\s+function|class)\b)')

# Convert top-level const/let → var to allow harmless re-declaration
# across bundled files (e.g. TWO_PI declared in both eq-designer and eq-graph).
# Only matches lines that START with const/let (not indented → top-level scope).
_TOP_CONST_LET_RE  = re.compile(r'^(const|let)\b', re.MULTILINE)


# ── Dependency collection (topological sort) ──────────────────────────────────

def _collect_deps(entry_path: str, web_dir: str, visited: set, order: list):
    """
    DFS post-order traversal of the ES module import graph.
    Result: `order` contains files with dependencies before dependents.
    CommonJS files (module.exports) are skipped — they are Electron-only.
    """
    real = os.path.realpath(entry_path)
    if real in visited:
        return
    visited.add(real)

    js_dir = os.path.dirname(entry_path)

    with open(entry_path, "r", encoding="utf-8") as f:
        src = f.read()

    # Skip CommonJS — not for browser
    if "module.exports" in src and "import " not in src:
        print(f"  [skip-cjs]   {os.path.basename(entry_path)}")
        return

    for m in _IMPORT_RE.finditer(src):
        mod_path = m.group(2)
        if mod_path.startswith("http") or mod_path.startswith("//"):
            continue
        resolved = _resolve_local(mod_path, js_dir, web_dir)
        if resolved is None:
            print(f"  [dep-warn]   {mod_path} (from {os.path.basename(entry_path)})")
            continue
        _collect_deps(resolved, web_dir, visited, order)

    order.append(entry_path)
    print(f"  [dep-order]  {os.path.basename(entry_path)}")


# ── Per-file transformation ───────────────────────────────────────────────────

def _transform(src: str) -> str:
    """
    Prepare one ES module file for inclusion in a flat <script>:

    1. Delete all import/re-export-from statements
       (dependencies are already concatenated above this file in the bundle).
    2. 'export default X'  →  'var __default__ = X'
    3. Strip 'export' keyword before const/let/var/function/class
    4. Convert top-level const/let → var
       Rationale: multiple files may declare the same helper name (e.g. TWO_PI).
       In a flat script, duplicate 'const' is a SyntaxError; duplicate 'var' is fine.
       This does NOT affect variables inside functions/classes (they are indented,
       so the regex anchored at ^ does not match them).
    """
    # Step 1 — remove import/re-export-from lines
    result = _IMPORT_STMT_RE.sub("", src)
    result = re.sub(
        r"""^[ \t]*import\s+(['"])[^'"]+\1[^\n]*\n?""",
        "", result, flags=re.MULTILINE
    )

    # Step 2 — export default
    result = _EXPORT_DEFAULT_RE.sub("var __default__ = ", result)

    # Step 3 — strip export keyword
    result = _EXPORT_NAMED_RE.sub("", result)

    # Step 4 — top-level const/let → var
    result = _TOP_CONST_LET_RE.sub("var", result)

    return result


# ── Bundler ───────────────────────────────────────────────────────────────────

def _bundle_js(entry_path: str, web_dir: str) -> str:
    """
    Build a single flat JS string from an ES module entry point.
    Files are emitted in topological order (deps first).
    """
    visited = set()
    order   = []
    _collect_deps(entry_path, web_dir, visited, order)

    parts = []
    for path in order:
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        if "module.exports" in src and "import " not in src:
            continue  # skip CJS
        rel = os.path.relpath(path, web_dir)
        parts.append(f"\n// === {rel} ===\n{_transform(src)}")

    return "\n".join(parts)


# ── HTML asset inliner ────────────────────────────────────────────────────────

def _inline_assets(html_path: str) -> bytes:
    base_dir = os.path.dirname(html_path)

    with open(html_path, "r", encoding="utf-8") as f:
        html = f.read()

    # Inline <link rel="stylesheet">
    def replace_css(m):
        href = m.group(1)
        if not href or href.startswith("http"):
            return m.group(0)
        full = _resolve_local(href, base_dir, WEB_APP_DIR)
        if full is None:
            print(f"  [css-warn]   not found: {href}")
            return m.group(0)
        with open(full, "r", encoding="utf-8") as cf:
            css = cf.read()
        print(f"  [inline-css] {_strip_query(href)} ({len(css):,} B)")
        return f"<style>{css}</style>"

    html = re.sub(
        r'<link\b[^>]*\brel=["\']stylesheet["\'][^>]*\bhref=["\']([^"\']+)["\'][^>]*/?>',
        replace_css, html, flags=re.IGNORECASE)
    html = re.sub(
        r'<link\b[^>]*\bhref=["\']([^"\']+)["\'][^>]*\brel=["\']stylesheet["\'][^>]*/?>',
        replace_css, html, flags=re.IGNORECASE)

    # Bundle + inline <script src="..."> / <script type="module" src="...">
    def replace_js(m):
        src_attr = m.group(1)
        if not src_attr or src_attr.startswith("http"):
            return m.group(0)
        full = _resolve_local(src_attr, base_dir, WEB_APP_DIR)
        if full is None:
            print(f"  [js-warn]    not found: {src_attr}")
            return m.group(0)
        print(f"  [bundle-js]  {_strip_query(src_attr)}")
        js = _bundle_js(full, WEB_APP_DIR)
        print(f"               -> {len(js):,} B bundled")
        # 'defer' restores the implicit defer of type="module":
        # script runs after HTML is fully parsed so DOMContentLoaded
        # and document.getElementById() work correctly.
        return f"<script defer>{js}</script>"

    html = re.sub(
        r'<script\b[^>]*\bsrc=["\']([^"\']+)["\'][^>]*>\s*</script>',
        replace_js, html, flags=re.IGNORECASE)

    return html.encode("utf-8")


# ── Header writer ─────────────────────────────────────────────────────────────

def _get_content_hash(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def _write_header(compressed: bytes, original_len: int):
    os.makedirs(INCLUDE_DIR, exist_ok=True)

    hex_lines = []
    chunk = []
    for b in compressed:
        chunk.append(f"0x{b:02X}")
        if len(chunk) == 16:
            hex_lines.append("    " + ", ".join(chunk))
            chunk = []
    if chunk:
        hex_lines.append("    " + ", ".join(chunk))

    ratio = len(compressed) * 100 // original_len if original_len else 0

    with open(OUTPUT_HEADER, "w", encoding="utf-8") as f:
        f.write("/**\n")
        f.write(" * @file embedded_ui.h\n")
        f.write(" * @brief Auto-generated by sync_web_ui.py - do not edit manually.\n")
        f.write(f" *        Original : {original_len:,} bytes\n")
        f.write(f" *        Gzip     : {len(compressed):,} bytes  ({ratio}% of original)\n")
        f.write(" */\n\n")
        f.write("#pragma once\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"static const uint32_t UI_HTML_ORIGINAL_LEN = {original_len}U;\n")
        f.write(f"static const uint32_t UI_HTML_GZ_LEN       = {len(compressed)}U;\n\n")
        f.write("static const uint8_t UI_HTML_GZ[] PROGMEM = {\n")
        f.write(",\n".join(hex_lines))
        f.write("\n};\n")


# ── Main ──────────────────────────────────────────────────────────────────────

def embed_ui():
    print("\n[WebUI-Embed] Starting...")

    html_path = _find_ui_file()
    if html_path is None:
        print(f"[WebUI-Embed] WARNING: index.html not found in {WEB_APP_DIR} - skipping.")
        return

    print(f"[WebUI-Embed] Source: {html_path}")
    raw_bytes    = _inline_assets(html_path)
    original_len = len(raw_bytes)

    new_hash = _get_content_hash(raw_bytes)
    old_hash = ""
    if os.path.exists(HASH_FILE):
        with open(HASH_FILE) as f:
            old_hash = f.read().strip()

    if new_hash == old_hash and os.path.exists(OUTPUT_HEADER):
        print("[WebUI-Embed] No changes - skipping.\n")
        return

    compressed = gzip.compress(raw_bytes, compresslevel=9)
    ratio = len(compressed) * 100 // original_len
    print(f"[WebUI-Embed] {original_len:,} B -> {len(compressed):,} B gzip ({ratio}%)")

    _write_header(compressed, original_len)
    print("[WebUI-Embed] Written: include/embedded_ui.h")

    with open(HASH_FILE, "w") as f:
        f.write(new_hash)

    print("[WebUI-Embed] Done.\n")


# Run immediately so embedded_ui.h exists before web_server.cpp compiles
embed_ui()
#env.AddPreAction("upload", embed_ui)