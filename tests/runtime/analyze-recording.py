#!/usr/bin/env python3
"""Analyses the Program recording made by the in-OBS self-test (phase 1).

Audio: per-100 ms Goertzel energy at the test tones
  440 Hz  commentary (global output channel)   -> must be continuous
  1000 Hz replay clip                           -> only while Replay is on Program
  2000 Hz highlights reel                       -> only while Highlights is on Program
Video: first-frame colour markers (replay = magenta, highlights = yellow) must appear
right after the take, proving the clip started from its beginning.

Usage: analyze-recording.py phase1.json      (needs ffmpeg + python3 only)
Exit status 0 when every check passes.
"""
import json
import math
import struct
import subprocess
import sys

RATE = 8000  # analysis sample rate (tones are far below Nyquist)
WIN = 0.1


def goertzel(samples, freq):
    k = 2.0 * math.cos(2.0 * math.pi * freq / RATE)
    s1 = s2 = 0.0
    for x in samples:
        s0 = x + k * s1 - s2
        s2, s1 = s1, s0
    p = s1 * s1 + s2 * s2 - k * s1 * s2
    return math.sqrt(max(p, 0.0)) / max(len(samples), 1)


def load_audio(path):
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
        check=True, capture_output=True).stdout
    n = len(raw) // 2
    return [v / 32768.0 for v in struct.unpack("<%dh" % n, raw[: n * 2])]


def tone_track(samples, freq):
    step = int(RATE * WIN)
    return [goertzel(samples[i:i + step], freq) for i in range(0, len(samples) - step, step)]


def frame_rgb(path, t):
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-ss", "%.3f" % t, "-i", path, "-frames:v", "1", "-vf", "scale=16:9",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], check=True, capture_output=True).stdout
    if len(raw) < 16 * 9 * 3:
        return None
    # centre pixel
    i = (4 * 16 + 8) * 3
    return raw[i], raw[i + 1], raw[i + 2]


def is_magenta(c):
    return c and c[0] > 150 and c[2] > 150 and c[1] < 110


def is_yellow(c):
    return c and c[0] > 150 and c[1] > 150 and c[2] < 110


def main():
    report = json.load(open(sys.argv[1]))
    path = report.get("recording")
    if not path:
        print("UNRUN: no recording in report")
        return 2
    ev = {}
    for e in report["timeline"]:
        ev.setdefault(e["event"], []).append(e["rec_ms"] / 1000.0)

    samples = load_audio(path)
    dur = len(samples) / RATE
    tracks = {f: tone_track(samples, f) for f in (440, 1000, 2000)}
    # Threshold: well above silence/noise floor, far below a full-scale tone (~0.25-0.5).
    thr = 0.01

    def present(f, a, b):
        tr = tracks[f]
        idx = range(max(0, int(a / WIN)), min(len(tr), int(b / WIN)))
        vals = [tr[i] > thr for i in idx]
        return (sum(vals) / len(vals)) if vals else None

    results = []

    def check(cid, title, ok, detail=""):
        results.append((cid, title, ok, detail))

    comm = present(440, 0.5, dur - 0.5)
    check("A1", "Commentary tone continuous across all camera and playback takes", comm is not None and comm > 0.98,
          "present in %.1f%% of 100 ms windows over %.0f s" % (100 * (comm or 0), dur))

    ow = (ev.get("offair_wait_start", [None])[0], ev.get("offair_wait_end", [None])[0])
    if None not in ow:
        r = present(1000, ow[0] + 0.3, ow[1])
        h = present(2000, ow[0] + 0.3, ow[1])
        check("A2", "No replay/highlights audio while Replay waits in Preview (%.0f s)" % (ow[1] - ow[0]),
              r == 0 and h == 0, "1 kHz %.1f%%, 2 kHz %.1f%%" % (100 * (r or 0), 100 * (h or 0)))
        c = frame_rgb(path, (ow[0] + ow[1]) / 2)
        check("V1", "Program shows the camera (not the clip) during the Preview wait", not is_magenta(c), str(c))

    ron = ev.get("replay_on_program", [None])[0]
    hon = ev.get("highlights_on_program", [None])[0]
    rl = ev.get("return_live", [None])[0]
    if ron is not None and hon is not None:
        r = present(1000, ron + 0.3, hon - 0.2)
        check("A3", "Replay audio on Program only after the take", r is not None and r > 0.9,
              "1 kHz in %.1f%% of the replay window" % (100 * (r or 0)))
        before = present(1000, 0.5, ron - 0.3)
        check("A4", "No replay audio before its first take (incl. loading / arming)", before == 0,
              "1 kHz %.1f%% before take" % (100 * (before or 0)))
        c = [frame_rgb(path, ron + d) for d in (0.15, 0.35, 0.6)]
        check("V2", "Replay starts from its first frame on take (magenta marker visible)",
              sum(1 for x in c if is_magenta(x)) >= 2, str(c))
    if hon is not None and rl is not None:
        h = present(2000, hon + 0.3, rl - 0.2)
        r = present(1000, hon + 0.4, rl - 0.2)
        check("A5", "Highlights audio on Program; replay audio stops when it leaves Program",
              h is not None and h > 0.9 and r == 0, "2 kHz %.1f%%, 1 kHz %.1f%%" % (100 * (h or 0), 100 * (r or 0)))
        c = [frame_rgb(path, hon + d) for d in (0.15, 0.35, 0.6)]
        check("V3", "Highlights starts from its first frame on take (yellow marker visible)",
              sum(1 for x in c if is_yellow(x)) >= 2, str(c))
        after = present(2000, rl + 0.4, rl + 3.0)
        check("A6", "Highlights audio stops after Return Live", after == 0, "2 kHz %.1f%%" % (100 * (after or 0)))

    ok = all(r[2] for r in results)
    for cid, title, passed, detail in results:
        print("%s %s: %s  [%s]" % ("PASS" if passed else "FAIL", cid, title, detail))
    print("recording: %s (%.1f s)" % (path, dur))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
