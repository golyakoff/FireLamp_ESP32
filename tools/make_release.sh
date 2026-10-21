#!/usr/bin/env bash
# Packs ESP-IDF build outputs into GitHub release assets and writes the release page text.
# Run from the repo root after building every target:
#
#   idf.py -B build-esp32   -DSDKCONFIG=build-esp32/sdkconfig   -DIDF_TARGET=esp32   build
#   idf.py -B build-esp32c3 -DSDKCONFIG=build-esp32c3/sdkconfig -DIDF_TARGET=esp32c3 build
#   tools/make_release.sh v0.8.0
#
# Produces in $OUT_DIR (default: release/), for each target T (esp32, esp32c3):
#   firelamp_<tag>_T_bootloader.bin   second-stage bootloader (0x1000 on ESP32, 0x0 on ESP32-C3)
#   firelamp_<tag>_T_pt.bin           partition table (0x8000)
#   firelamp_<tag>_T_otadata.bin      otadata: boot from ota_0 (0xe000)
#   firelamp_<tag>_T_fw.bin           application (0x20000), also used for BLE and internet OTA
#   firelamp_<tag>_T_full.bin         all of the above merged into one image, written at 0x0
#   flash_T.bat                       Windows flashing script
#   firelamp_<tag>_T.zip              the separate images + flash_T.bat
#   manifest.json                     internet update manifest (see main/updater.h)
# and the release page markdown in $NOTES_FILE (default: release_notes.md).
#
# Flash offsets and options are taken from build-T/flasher_args.json, not hardcoded.
# Requires: python with esptool >= 5 (`pip install esptool`).
set -euo pipefail

TAG="${1:?usage: $0 <tag>}"
OUT="${OUT_DIR:-release}"
NOTES="${NOTES_FILE:-release_notes.md}"
PYTHON="${PYTHON:-python}"
REPO="${GITHUB_REPOSITORY:-golyakoff/FireLamp_ESP32}"
VARIANT="default"
PREFIX="firelamp_${TAG}"
BUILD_DATE="$(git log -1 --format=%cI 2>/dev/null || date -Iseconds)"
BASE_URL="https://github.com/${REPO}/releases/download/${TAG}"

# The tag must match the version compiled into the firmware (version.txt): the internet
# update compares it with the version in the manifest
VERSION="$(head -n1 version.txt | tr -d '[:space:]')"
[ "$TAG" = "v$VERSION" ] || { echo "tag $TAG does not match version.txt ($VERSION)" >&2; exit 1; }

# target | chip name for the release page
TARGETS=(
  "esp32|ESP32 (e.g. ESP32-WROOM-32D)"
  "esp32c3|ESP32-C3 (e.g. ESP32-C3-FH4)"
)

rm -rf "$OUT"
mkdir -p "$OUT"

# Reads a value from flasher_args.json: flash_args <build dir> <python expression on `j`>
flash_arg() {
  "$PYTHON" -c "import json,sys; j=json.load(open(sys.argv[1]+'/flasher_args.json')); print($2)" "$1"
}

# Writes flash_<target>.bat (CRLF line endings, cmd.exe syntax)
write_bat() {
  local t="$1" chip="$2" opts="$3" boot="$4" pt="$5" ota="$6" app="$7" bat="$OUT/flash_$1.bat"
  cat > "$bat" <<EOF
@echo off
rem FireLamp ${TAG} for ${chip} - flash over USB with esptool.
rem Usage: flash_${t}.bat [COMx]     (no port given = auto-detect)
rem Keep this file in the same folder as the .bin files.
cd /d "%~dp0"

set BAUD=460800
set PORTARG=
if not "%~1"=="" set PORTARG=--port %~1

python --version >nul 2>&1 || (
  echo Python not found. Install it from https://www.python.org/downloads/
  echo and tick "Add python.exe to PATH" in the installer.
  goto :fail
)
python -m esptool version >nul 2>&1 || (
  echo esptool not found, installing it with pip...
  python -m pip install --upgrade esptool || goto :fail
)

rem ${boot} bootloader, ${pt} partition table, ${ota} otadata (boot ota_0), ${app} application.
rem The nvs partition (0x9000, lamp settings) is not touched.
python -m esptool --chip ${t} %PORTARG% --baud %BAUD% write-flash ${opts} ^
  ${boot} ${PREFIX}_${t}_bootloader.bin ^
  ${pt} ${PREFIX}_${t}_pt.bin ^
  ${ota} ${PREFIX}_${t}_otadata.bin ^
  ${app} ${PREFIX}_${t}_fw.bin
if errorlevel 1 goto :fail

echo.
echo === DONE: the lamp restarts, pair the phone within 2 minutes ===
pause
exit /b 0

:fail
echo.
echo === FLASH FAILED ===
echo - Stuck at "Connecting...": hold the BOOT button on the board while plugging in USB.
echo - Write errors: lower the speed, edit "set BAUD=115200" in this file.
echo - No COM port: install the USB-UART driver (CP210x or CH340), ESP32-C3 with native USB
echo   needs no driver. Pass the port explicitly, e.g. flash_${t}.bat COM4
pause
exit /b 1
EOF
  sed -i 's/$/\r/' "$bat"
}

MANIFEST_BUILDS=()
TABLE_ROWS=""
FLASH_MAP=""
for entry in "${TARGETS[@]}"; do
  IFS='|' read -r t chip <<< "$entry"
  b="build-$t"
  [ -f "$b/flasher_args.json" ] || { echo "missing $b/flasher_args.json (build target $t first)" >&2; exit 1; }
  [ "$(flash_arg "$b" "j['extra_esptool_args']['chip']")" = "$t" ] || { echo "$b is not built for $t" >&2; exit 1; }

  # offsets and files from the build
  boot=$(flash_arg "$b" "j['bootloader']['offset']")
  pt=$(flash_arg "$b" "j['partition-table']['offset']")
  ota=$(flash_arg "$b" "j['otadata']['offset']")
  app=$(flash_arg "$b" "j['app']['offset']")
  opts=$(flash_arg "$b" "' '.join(a.replace('_', '-', 1) if a.startswith('--') else a for a in j['write_flash_args'])")

  cp "$b/$(flash_arg "$b" "j['bootloader']['file']")"      "$OUT/${PREFIX}_${t}_bootloader.bin"
  cp "$b/$(flash_arg "$b" "j['partition-table']['file']")" "$OUT/${PREFIX}_${t}_pt.bin"
  cp "$b/$(flash_arg "$b" "j['otadata']['file']")"         "$OUT/${PREFIX}_${t}_otadata.bin"
  cp "$b/$(flash_arg "$b" "j['app']['file']")"             "$OUT/${PREFIX}_${t}_fw.bin"

  # single image written at 0x0; gaps (incl. nvs) are filled with 0xFF
  # shellcheck disable=SC2086
  "$PYTHON" -m esptool --chip "$t" merge-bin -o "$OUT/${PREFIX}_${t}_full.bin" $opts \
    "$boot" "$OUT/${PREFIX}_${t}_bootloader.bin" \
    "$pt"   "$OUT/${PREFIX}_${t}_pt.bin" \
    "$ota"  "$OUT/${PREFIX}_${t}_otadata.bin" \
    "$app"  "$OUT/${PREFIX}_${t}_fw.bin"

  write_bat "$t" "$chip" "$opts" "$boot" "$pt" "$ota" "$app"

  (cd "$OUT" && "$PYTHON" -m zipfile -c "${PREFIX}_${t}.zip" \
    "flash_${t}.bat" \
    "${PREFIX}_${t}_bootloader.bin" \
    "${PREFIX}_${t}_pt.bin" \
    "${PREFIX}_${t}_otadata.bin" \
    "${PREFIX}_${t}_fw.bin")

  MANIFEST_BUILDS+=("${t}-${VARIANT}=$OUT/${PREFIX}_${t}_fw.bin:${PREFIX}_${t}_fw.bin")
  TABLE_ROWS+="| ${chip} | \`${PREFIX}_${t}.zip\` | \`${PREFIX}_${t}_fw.bin\` |
"
  FLASH_MAP+="| ${chip} | \`${boot}\` | \`${pt}\` | \`${ota}\` | \`${app}\` |
"
  eval "OPTS_$t=\"\$opts\" BOOT_$t=\$boot PT_$t=\$pt OTA_$t=\$ota APP_$t=\$app"
done

"$PYTHON" tools/make_manifest.py --version "$TAG" --base-url "$BASE_URL" "${MANIFEST_BUILDS[@]}" > "$OUT/manifest.json"

# ---------------------------------------------------------------- release page
P="$PREFIX"
cat > "$NOTES" <<EOF
# FireLamp ESP32 Firmware

**Version:** ${TAG}
**Build date:** ${BUILD_DATE}

## Which files do I need?

| Chip | First install over USB | Update from the phone (BLE) |
|---|---|---|
${TABLE_ROWS}
One build per chip fits every lamp with the reference pinout (see the README): the DS3231 RTC
and the EEPROM are detected automatically, the control elements (one button, four buttons,
touch pad), the buttons type and the matrix (size, type, orientation, wiring) are set from the app.
A board with other pins needs its own build; the internet update never installs these files
on it (see "Build variant" in the README).

Already running FireLamp? Update from the app: over BLE, or "install update" when the lamp has
WiFi. Settings are kept.

## First install on Windows

1. **Install Python 3** from https://www.python.org/downloads/ - in the installer tick
   **"Add python.exe to PATH"**.
2. **Install esptool**: open a command prompt (\`Win+R\` → \`cmd\`) and run
   \`python -m pip install --upgrade esptool\`.
3. **Connect the board** with a USB data cable. ESP32 boards appear in Device Manager under
   *Ports (COM & LPT)* via a CP210x or CH340 USB-UART chip (install its driver if there is no
   port). ESP32-C3 with native USB appears as *USB Serial Device*, no driver needed.
4. **Download and unpack** the zip for your chip.
5. **Run** \`flash_esp32.bat\` / \`flash_esp32c3.bat\` (double-click), or pass the port:
   \`flash_esp32c3.bat COM5\`. If it hangs at \`Connecting...\`, hold the **BOOT** button while
   plugging in USB.

After \`=== DONE ===\` the lamp restarts. Pair the phone within 2 minutes (later: 5 clicks of the
main button open the pairing window again).

## Linux / macOS

\`\`\`bash
python3 -m pip install --upgrade esptool
# ESP32-C3
python3 -m esptool --chip esp32c3 write-flash ${OPTS_esp32c3} \\
  ${BOOT_esp32c3} ${P}_esp32c3_bootloader.bin ${PT_esp32c3} ${P}_esp32c3_pt.bin \\
  ${OTA_esp32c3} ${P}_esp32c3_otadata.bin ${APP_esp32c3} ${P}_esp32c3_fw.bin
# ESP32
python3 -m esptool --chip esp32 write-flash ${OPTS_esp32} \\
  ${BOOT_esp32} ${P}_esp32_bootloader.bin ${PT_esp32} ${P}_esp32_pt.bin \\
  ${OTA_esp32} ${P}_esp32_otadata.bin ${APP_esp32} ${P}_esp32_fw.bin
\`\`\`

Add \`--port /dev/ttyUSB0\` / \`/dev/ttyACM0\` (Linux) or \`--port /dev/cu.usbserial-XXXX\` (macOS) if
the port is not detected automatically.

## Single-file image

\`${P}_<chip>_full.bin\` is all images merged into one, written at address **0x0**:

\`\`\`bash
python -m esptool --chip esp32c3 write-flash 0x0 ${P}_esp32c3_full.bin
\`\`\`

It also works with browser-based flashers such as https://espressif.github.io/esptool-js/
(offset \`0x0\`). ⚠️ The merged image overwrites the \`nvs\` partition, so the lamp's settings and
paired phones are reset - use it for a clean install, not for updates.

## Flash map

| Chip | Bootloader | Partition table | otadata | Application (\`ota_0\`) |
|---|---|---|---|---|
${FLASH_MAP}
The \`nvs\` partition at \`0x9000\` (settings, paired phones) is not written by the .bat.
\`*_fw.bin\` is the application only - never write it at the bootloader address. Note that the
ESP32-C3 bootloader goes to **0x0**, the ESP32 one to **0x1000**.

## Internet update

\`manifest.json\` describes this release for the lamp's internet update (version, file URLs,
sizes, SHA-256 per chip and build variant).
EOF

echo "=== Release assets in $OUT ==="
ls -la "$OUT"
echo "=== Release notes: $NOTES ==="
