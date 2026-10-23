#!/usr/bin/env bash
# Runs the in-OBS automated self-test (phase 1: fresh profile, phase 2: after restart)
# against a self-test build of the plugin, in an isolated OBS configuration.
#
# Usage: tests/runtime/run-selftest.sh <build-dir> <work-dir>
#   Build first with: cmake --preset ubuntu-x86_64 -DMF_ENABLE_SELFTEST=ON && cmake --build build_x86_64
# Env: DISPLAY (an X server; starts Xvfb :99 if unset), PERF_SECONDS, SOAK_SECONDS, OFFAIR_SECONDS
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$(cd "${1:?build dir}" && pwd)"
WORK="$(mkdir -p "${2:?work dir}" && cd "$2" && pwd)"
NAME=matchframe-multiview

rm -rf "$WORK/config" "$WORK/rec" && mkdir -p "$WORK/config/obs-studio" "$WORK/rec"
PLUG="$WORK/config/obs-studio/plugins/$NAME"
mkdir -p "$PLUG/bin/64bit" "$PLUG/data"
cp "$BUILD/$NAME.so" "$PLUG/bin/64bit/"
cp -r "$ROOT/data/." "$PLUG/data/"
cat > "$WORK/config/obs-studio/global.ini" <<INI
[General]
FirstRun=true
EnableAutoUpdates=false
ConfirmOnExit=false
[BasicWindow]
SceneDuplicationMode=true
SwapScenesMode=true
EditPropertiesMode=false
INI

if [ -z "${DISPLAY:-}" ]; then
  Xvfb :99 -screen 0 1920x1080x24 >/dev/null 2>&1 &
  XVFB=$!; trap 'kill $XVFB 2>/dev/null || true' EXIT
  export DISPLAY=:99; sleep 2
fi

export XDG_CONFIG_HOME="$WORK/config"
export MATCHFRAME_SELFTEST_MEDIA="$ROOT/tests/media"
export MATCHFRAME_SELFTEST_RECDIR="$WORK/rec"
export MATCHFRAME_SELFTEST_PERF_SECONDS="${PERF_SECONDS:-30}"
export MATCHFRAME_SELFTEST_SOAK_SECONDS="${SOAK_SECONDS:-0}"
export MATCHFRAME_SELFTEST_OFFAIR_SECONDS="${OFFAIR_SECONDS:-60}"
# This OBS may lack Browser Source (e.g. distro builds without CEF): exercise the shared
# graphics code paths with a stand-in colour source and report Browser checks as unrun.
if [ ! -e /usr/lib/x86_64-linux-gnu/obs-plugins/obs-browser.so ] && [ ! -e /usr/lib/obs-plugins/obs-browser.so ]; then
  export MATCHFRAME_TEST_GRAPHICS_SOURCE=color_source_v3
fi

for PHASE in 1 2; do
  echo "== phase $PHASE"
  MATCHFRAME_SELFTEST_PHASE=$PHASE MATCHFRAME_SELFTEST_OUT="$WORK/phase$PHASE.json" \
    timeout "${OBS_TIMEOUT:-1800}" obs --disable-shutdown-check --disable-updater --multi \
    >"$WORK/obs-phase$PHASE.stdout" 2>&1 || echo "obs exited with $?"
  cp "$(ls -t "$WORK"/config/obs-studio/logs/*.txt | head -1)" "$WORK/obs-phase$PHASE.log"
  python3 -c "import json,sys; d=json.load(open('$WORK/phase$PHASE.json')); print('phase $PHASE: %d passed, %d failed' % (d['pass'], d['fail']))"
done
python3 "$ROOT/tests/runtime/analyze-recording.py" "$WORK/phase1.json" | tee "$WORK/recording-analysis.txt"
