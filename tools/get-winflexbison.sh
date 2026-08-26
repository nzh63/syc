#!/bin/sh
# Download the vendored WinFlexBison binaries needed to build syc on Windows.
# Only required on Windows; kept out of git via .gitignore.
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)/external"
URL=https://ghfast.top/https://github.com/lexxmark/winflexbison/releases/download/v2.5.24/win_flex_bison-2.5.24.zip
mkdir -p "$DIR/winflexbison" "$DIR/tmp"
curl -fL -o "$DIR/tmp/wfb.zip" "$URL" || curl -fL -o "$DIR/tmp/wfb.zip" \
    https://github.com/lexxmark/winflexbison/releases/download/v2.5.24/win_flex_bison-2.5.24.zip
unzip -o "$DIR/tmp/wfb.zip" -d "$DIR/winflexbison"
rm -rf "$DIR/tmp"
"$DIR/winflexbison/win_bison.exe" --version
