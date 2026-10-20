#!/bin/sh
# Publish release v<version> on GitHub (minusro/loopbrother): build the Mac
# installer here and create the release with it, on the commit that's on
# GitHub's main, its notes the version's entry in CHANGELOG.md. Publishing
# the release starts the Windows workflow, which builds its installer and
# attaches it to the same release.
#
# Usage, from the project root:   sh installer/release.sh
# Needs the GitHub CLI logged in (gh auth status) and everything committed
# and pushed. The version comes from project(... VERSION) in CMakeLists.txt.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
ver=$(sed -n 's/^project(junglemassive VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
tag="v$ver"
pkg="dist/LOOP-BROTHER-$ver-macos.pkg"

gh auth status >/dev/null 2>&1 || { echo "log in first: gh auth login (or gh auth refresh -h github.com)"; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "commit (and push) everything first"; exit 1; }
git fetch -q origin
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "push first: main and origin/main differ"; exit 1; }
if gh release view "$tag" >/dev/null 2>&1; then echo "$tag is already released: bump the version"; exit 1; fi
# What's new: the version's entry in CHANGELOG.md, from below its heading
# ("## v<version> — <date>") to the next version's, one line per paragraph
# or list item: GitHub keeps line breaks in release notes, so the file's
# wrapping would show.
changes=$(awk -v h="## $tag " 'index($0, h) == 1 { on = 1; next } on && /^## / { exit } on' CHANGELOG.md \
    | sed '/./,$!d' \
    | awk '/^$/ || /^#/ { if (line != "") print line; line = ""; print; next }
           /^- /        { if (line != "") print line; line = $0; next }
                        { sub(/^ +/, ""); line = (line == "") ? $0 : line " " $0 }
           END          { if (line != "") print line }')
[ -n "$changes" ] || { echo "add $tag to CHANGELOG.md first"; exit 1; }

sh installer/macos/make_pkg.sh

notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
cat > "$notes" <<NOTES
LÖÖP BRÖTHER $ver: VST3, Audio Unit and standalone app for macOS (universal), VST3 and app for Windows (64-bit).

## What's new

$changes

The installers aren't signed yet: see **Installing** in the [README](https://github.com/minusro/loopbrother#installing) for getting past the macOS and Windows warnings. The Windows installer is added by the build a few minutes after this release appears.
NOTES
gh release create "$tag" "$pkg" --target "$(git rev-parse HEAD)" --title "LÖÖP BRÖTHER $ver" --notes-file "$notes"
echo "== released $tag; the Windows installer follows from the workflow"
