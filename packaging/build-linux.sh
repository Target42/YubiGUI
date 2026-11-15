#!/bin/bash
# Baut yubigui gegen das Qt 6 der Distribution.
# Schreibt build/linux/yubigui und build/linux/depends.txt.
# Im Ubuntu-Container ist das Skript root und installiert die Werkzeuge selbst.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/packaging/VERSION")"
WORK="/tmp/yubigui-build"
OUT="$ROOT/build/linux"

if ! command -v qmake6 >/dev/null 2>&1 && ! command -v qmake >/dev/null 2>&1; then
    if [ "$(id -u)" -ne 0 ]; then
        echo "qmake6 fehlt. packaging/build-linux.ps1 startet dafür einen Ubuntu-Container." >&2
        exit 1
    fi
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
        build-essential qt6-base-dev qt6-qpa-plugins ca-certificates
fi

QMAKE="$(command -v qmake6 || true)"
if [ -z "$QMAKE" ]; then
    QMAKE="$(command -v qmake)"
fi

rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"
"$QMAKE" "$ROOT/YubiGUI.pro" "CONFIG+=release" "CONFIG-=debug_and_release" "VERSION=$VERSION"
make -j"$(nproc)"

if [ -x "$WORK/yubigui" ]; then
    BIN="$WORK/yubigui"
elif [ -x "$WORK/release/yubigui" ]; then
    BIN="$WORK/release/yubigui"
else
    echo "Die Datei yubigui wurde nicht erzeugt." >&2
    find "$WORK" -type f -name 'yubigui' >&2 || true
    exit 1
fi

mkdir -p "$OUT"
cp -a "$BIN" "$OUT/yubigui"
chmod 755 "$OUT/yubigui"

# Mindestversionen aus den Symbolversionen, nicht aus der vollen Paketversion.
mkdir -p "$WORK/debian"
cat > "$WORK/debian/control" <<'EOF'
Source: yubigui

Package: yubigui
Architecture: amd64
Description: placeholder
 placeholder
EOF
(
    cd "$WORK"
    dpkg-shlibdeps -O -e"$OUT/yubigui"
) > "$OUT/shlibs.txt"

deps_line="$(sed -n 's/^shlibs:Depends=//p' "$OUT/shlibs.txt" | head -n 1)"
if [ -z "$deps_line" ]; then
    echo "dpkg-shlibdeps lieferte keine Abhängigkeiten." >&2
    cat "$OUT/shlibs.txt" >&2
    exit 1
fi

: > "$OUT/depends.txt"
IFS=',' read -ra parts <<< "$deps_line"
for part in "${parts[@]}"; do
    part="$(printf '%s' "$part" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    [ -n "$part" ] && printf '%s\n' "$part" >> "$OUT/depends.txt"
done
if ! grep -q '^qt6-qpa-plugins' "$OUT/depends.txt"; then
    printf '%s\n' 'qt6-qpa-plugins (>= 6.4.2)' >> "$OUT/depends.txt"
fi
sort -u "$OUT/depends.txt" -o "$OUT/depends.txt"

set +e
QT_QPA_PLATFORM=offscreen timeout 4 "$OUT/yubigui" >"$OUT/smoke.txt" 2>&1
code=$?
set -e
if [ "$code" -ne 124 ]; then
    echo "Startprüfung fehlgeschlagen (Exit $code)." >&2
    cat "$OUT/smoke.txt" >&2
    exit 1
fi

echo "linux-binary-ok $VERSION"
