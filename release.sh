#!/usr/bin/env bash
# Firmware-Release der Station: baut, taggt vX.Y.Z, pusht und erstellt
# ein GitHub-Release mit der firmware.bin als Download-Asset.
#
# Aufruf:  bash release.sh
# Version: kommt aus src/NowStation.h (STATION_FW_MAJOR/MINOR/PATCH) –
#          vorher BEWUSST hochzaehlen und committen (siehe CLAUDE.md).
set -euo pipefail
cd "$(dirname "$0")"

export PATH="$HOME/.platformio/penv/bin:$PATH"

# --- Version aus den Quellen lesen -------------------------------------------
MAJOR=$(sed -n 's/.*STATION_FW_MAJOR = \([0-9]*\).*/\1/p' src/NowStation.h)
MINOR=$(sed -n 's/.*STATION_FW_MINOR = \([0-9]*\).*/\1/p' src/NowStation.h)
PATCH=$(sed -n 's/.*STATION_FW_PATCH = \([0-9]*\).*/\1/p' src/NowStation.h)
VER="v${MAJOR}.${MINOR}.${PATCH}"
ASSET="infinitag-station-${VER}.bin"

# --- Vorbedingungen -----------------------------------------------------------
if [[ -n "$(git status --porcelain)" ]]; then
  echo "FEHLER: Working tree nicht sauber – erst committen." >&2
  exit 1
fi
if git rev-parse "$VER" >/dev/null 2>&1; then
  echo "FEHLER: Tag $VER existiert schon – erst STATION_FW_PATCH in src/NowStation.h erhoehen." >&2
  exit 1
fi

# --- Bauen ---------------------------------------------------------------------
echo "== Baue Station $VER =="
pio run
# Build-Ordner liegt wegen workspace_dir (SMB-Workaround) unter ~/.pio-workspaces
BIN="$HOME/.pio-workspaces/infinitag-now-station/build/esp32-s3-devkitc-1/firmware.bin"
[[ -f "$BIN" ]] || BIN=".pio/build/esp32-s3-devkitc-1/firmware.bin"
[[ -f "$BIN" ]] || { echo "FEHLER: firmware.bin nicht gefunden." >&2; exit 1; }
mkdir -p dist
cp "$BIN" "dist/$ASSET"

# --- Taggen, pushen, Release ---------------------------------------------------
PREV=$(git describe --tags --abbrev=0 2>/dev/null || true)
if [[ -n "$PREV" ]]; then
  NOTES=$(git log --oneline "${PREV}..HEAD" | sed 's/^/- /')
else
  NOTES=$(git log --oneline -10 | sed 's/^/- /')
fi

git tag "$VER"
git push origin main --tags
gh release create "$VER" "dist/$ASSET" \
  --title "Station $VER" \
  --notes "Firmware-Update per SoftAP: Config-Box → Geraete-Menue → Update (OTA), dann ${ASSET} auf http://192.168.4.1 hochladen.

Aenderungen:
${NOTES}"

echo "== Release $VER erstellt =="
gh release view "$VER" --json url -q .url
