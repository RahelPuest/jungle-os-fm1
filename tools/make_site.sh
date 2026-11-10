#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The release VERSION of both firmwares and the GitHub Pages site (release/site, deployed by .github/workflows/pages.yml):
#   release/NAME-VERSION.fwsc, release/SHA256SUMS
#   release/site/index.html            JUNGLE OS's front page (site/landing.md)
#   release/site/live/                 the live set (docs/user-guide/live-performance.md)
#   release/site/break-surgeon/, abyss-engine/   each firmware's site (fm1-base tools/make_site.py): its front page,
#                                      the browser emulator (emu/), the manual, the web installer, the package
#   tools/make_site.sh VERSION         (needs the device toolchain, Docker and Emscripten: docs/development/setup.md)
set -e
V="${1:?usage: tools/make_site.sh VERSION (X.Y[.Z][-suffix])}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/env.sh"
command -v emcc >/dev/null || { echo "make_site: no emcc (Emscripten) for the browser emulator" >&2; exit 1; }
cd "$ROOT"
OUT=release/site
rm -rf "$OUT"
mkdir -p "$OUT"
for fw in break-surgeon:breaksurgeon abyss-engine:abyssengine; do
    dir=${fw%%:*} name=${fw##*:}
    app="$ROOT/firmware/$dir"
    python3 base/tools/build.py "$app" --release "$V" | grep -E '^  (ok|FAIL)|^package|rror' || true
    python3 base/tools/build.py "$app" --web --release "$V" | grep -E '^emu|rror' || true
    pkg="$app/build/$name-$V.fwsc"
    [ -f "$pkg" ] || { echo "make_site: no $pkg" >&2; exit 1; }
    cp "$pkg" release/
    python3 base/tools/make_site.py "$app" "$pkg" "$V" "$ROOT/$OUT/$dir"
    for old in release/$name-*.fwsc; do [ -f "$OUT/$dir/firmware/$(basename "$old")" ] || cp "$old" "$OUT/$dir/firmware/"; done
    # the pages show fm1.toml's name, which is also the file stem (lower case, one word): the shown name instead,
    # identifiers, storage keys and file names left as they are
    shown=$(printf '%s' "$dir" | tr 'a-z-' 'A-Z ')
    find "$OUT/$dir" \( -name '*.html' -o -name 'app.js' \) -exec \
        perl -pi -e "s/(?<!id: \")$name(?!-emu-|-\\d|\\.fwsc)/$shown/g" {} +
done
# JUNGLE OS's own pages (fm1-base's Markdown to HTML, the same look as the firmwares' pages)
REPO=https://github.com/RahelPuest/jungle-os-fm1
sed "s/{{VERSION}}/$V/g" site/landing.md > "$OUT/_landing.md"
python3 base/tools/manual_html.py "$OUT/_landing.md" "$OUT/index.html" --name "JUNGLE OS" --repo "$REPO" \
    --tagline "a drum & bass live rig of two FM-1s" --home
rm "$OUT/_landing.md"
# its bar: the two firmwares and the live set (the page is made as a firmware's, with emu/ install/ manual/ beside it)
python3 - "$OUT/index.html" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = '<a href="emu/">Emulator</a><a href="install/">Install</a><a href="manual/">Manual</a>'
if old not in s:
    sys.exit("make_site: the front page's bar is not as expected")
s = s.replace(old, '<a href="break-surgeon/">Break Surgeon</a><a href="abyss-engine/">Abyss Engine</a><a href="live/">Live set</a>')
open(p, "w", encoding="utf-8").write(s)
PY
mkdir -p "$OUT/live"
python3 base/tools/manual_html.py docs/user-guide/live-performance.md "$OUT/live/index.html" --name "JUNGLE OS" \
    --repo "$REPO" --tagline "the live set"
# JUNGLE OS's look (site/dnb.css) linked into every page, with the page's instrument for its colours
cp site/dnb.css "$OUT/dnb.css"
python3 - "$OUT" <<'PY'
import os, re, sys
out = sys.argv[1]
for dp, _, fs in os.walk(out):
    for f in fs:
        if not f.endswith(".html"):
            continue
        p = os.path.join(dp, f)
        rel = os.path.relpath(p, out)
        fw = "bs" if rel.startswith("break-surgeon") else "ab" if rel.startswith("abyss-engine") else "os"
        up = "../" * rel.count(os.sep)
        s = open(p, encoding="utf-8").read()
        if "dnb.css" in s:
            continue
        s, n = re.subn(r"</head>", f'<link rel="stylesheet" href="{up}dnb.css">\n</head>', s, count=1)
        s, m = re.subn(r"<html(\s|>)", f'<html data-fw="{fw}"\\1', s, count=1)
        if not (n and m):
            sys.exit(f"make_site: {p}: no <html> or </head> to style")
        open(p, "w", encoding="utf-8").write(s)
PY
touch "$OUT/.nojekyll"
(cd release && shasum -a 256 *.fwsc > SHA256SUMS)
echo "make_site: release/site ready for $V"
