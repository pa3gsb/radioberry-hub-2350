"""Embed the offline browser assets in flash; standard-library build tool."""
import pathlib
import sys
import zipfile
import io
import re

root = pathlib.Path(__file__).parent
browser = root / "browser"
source = browser / "src"
modules = ["vendor/ubersdr/palettes.js", "vendor/ubersdr/spectrumTrace.js",
           "vendor/ubersdr/waterfallRing.js", "board.js"]
parts = []
for module in modules:
    text = (source / module).read_text(encoding="utf-8")
    if re.search(r"^import\s", text, re.M):
        raise ValueError(f"Unexpected import in {module}")
    parts.append(re.sub(r"^export (?=(?:function|const)\s)", "", text, flags=re.M))
script = "(() => {\n'use strict';\n" + "\n".join(parts) + "\n" + (source / "app.js").read_text(encoding="utf-8") + "\n})();"
if "</script" in script.lower():
    raise ValueError("Unescaped closing script tag")
html = (source / "index.template.html").read_text(encoding="utf-8").replace(
    "/* INLINE_STYLE */", (source / "style.css").read_text(encoding="utf-8")).replace("/* INLINE_SCRIPT */", script)
archive = io.BytesIO()
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for path in sorted((browser / "src").rglob("*")):
        if path.is_file():
            info = zipfile.ZipInfo(str(path.relative_to(browser)).replace("\\", "/"))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, path.read_bytes())
    for name in ("build.ps1", "README.md", "LICENSE-ubersdr.txt", "iq-probe.html", "iq-stream.js", "iq-probe.js", "pcm-probe.html", "pcm-stream.js", "pcm-probe.js"):
        z.writestr(zipfile.ZipInfo(name), (browser / name).read_bytes())
assets = [
    ("/", "text/html; charset=utf-8", html.encode("utf-8")),
    ("/LICENSE-ubersdr.txt", "text/plain; charset=utf-8", (browser / "LICENSE-ubersdr.txt").read_bytes()),
    ("/browser-source.zip", "application/zip", archive.getvalue()),
    ("/iq-probe.html", "text/html; charset=utf-8", (browser / "iq-probe.html").read_bytes()),
    ("/iq-stream.js", "text/javascript; charset=utf-8", (browser / "iq-stream.js").read_bytes()),
    ("/iq-probe.js", "text/javascript; charset=utf-8", (browser / "iq-probe.js").read_bytes()),
    ("/pcm-probe.html", "text/html; charset=utf-8", (browser / "pcm-probe.html").read_bytes()),
    ("/pcm-stream.js", "text/javascript; charset=utf-8", (browser / "pcm-stream.js").read_bytes()),
    ("/pcm-probe.js", "text/javascript; charset=utf-8", (browser / "pcm-probe.js").read_bytes()),
]
lines = ['#include "websdr_service.h"']
for i, (_, _, data) in enumerate(assets):
    lines.append(f"static const uint8_t asset_{i}[] = {{")
    lines.extend(",".join(str(v) for v in data[j:j + 24]) + "," for j in range(0, len(data), 24))
    lines.append("};")
lines.append("const rb_web_asset rb_web_assets[] = {")
for i, (path, mime, _) in enumerate(assets):
    lines.append(f'{{"{path}", "{mime}", asset_{i}, sizeof(asset_{i})}},')
lines.append('{"/index.html", "text/html; charset=utf-8", asset_0, sizeof(asset_0)},')
lines.append("};\nconst size_t rb_web_asset_count = sizeof(rb_web_assets) / sizeof(rb_web_assets[0]);")
pathlib.Path(sys.argv[1]).write_text("\n".join(lines) + "\n", encoding="utf-8")
