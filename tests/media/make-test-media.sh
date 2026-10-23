#!/usr/bin/env bash
# Generates the MatchFrame Multiview test media with ffmpeg (drawtext needs libfreetype).
#  cam1..cam8.mp4   8 s, silent, distinct hue + large camera number + moving box + timecode
#  replay.mp4       8 s, 1 kHz tone; second 0 is solid MAGENTA (first-frame marker), frame counter
#  highlights.mp4  12 s, 2 kHz tone; second 0 is solid YELLOW (first-frame marker), frame counter
#  commentary.wav  10 s, 440 Hz tone (exact cycles -> seamless loop), stands in for commentary
set -euo pipefail
cd "$(dirname "$0")"
FONT="${FONT:-$(fc-match -f '%{file}' 'DejaVu Sans:bold' 2>/dev/null || true)}"
[ -f "$FONT" ] || { echo "No font found; set FONT=/path/to/font.ttf" >&2; exit 1; }
W=640; H=360; R=30
colors=(0x8B1A1A 0x8B5A00 0x6B8B00 0x1A8B2A 0x007B8B 0x1A3A9B 0x5A1A9B 0x8B1A6B)
for i in 1 2 3 4 5 6 7 8; do
  col=${colors[$((i-1))]}
  ffmpeg -v error -y -f lavfi -i "color=c=${col}:s=${W}x${H}:r=${R}:d=8" -f lavfi -i "testsrc2=s=160x90:r=${R}:d=8" \
    -filter_complex "[0][1]overlay=x='(W-w)*(0.5+0.45*sin(2*PI*t/4+${i}))':y='(H-h)*(0.5+0.45*cos(2*PI*t/4+${i}))'[ov];\
[ov]drawtext=fontfile=${FONT}:text='CAM ${i}':fontsize=110:fontcolor=white:borderw=6:x=30:y=30,\
drawtext=fontfile=${FONT}:text='%{pts\:hms}':fontsize=28:fontcolor=white:borderw=3:x=30:y=h-50" \
    -c:v libx264 -preset veryfast -crf 30 -pix_fmt yuv420p -g 30 -an "cam${i}.mp4"
done

clip() { # name dur firstcolor freq
  local name=$1 dur=$2 first=$3 freq=$4
  ffmpeg -v error -y -f lavfi -i "color=c=0x202060:s=${W}x${H}:r=${R}:d=${dur}" \
    -f lavfi -i "sine=frequency=${freq}:sample_rate=48000:duration=${dur}" \
    -vf "drawbox=x=0:y=0:w=iw:h=ih:color=${first}@1:t=fill:enable='lt(t,1)',\
drawtext=fontfile=${FONT}:text='${name^^}':fontsize=80:fontcolor=white:borderw=5:x=30:y=30,\
drawtext=fontfile=${FONT}:text='F %{frame_num}  %{pts\:hms}':fontsize=44:fontcolor=white:borderw=4:x=30:y=h-80" \
    -c:v libx264 -preset veryfast -crf 28 -pix_fmt yuv420p -g 15 -c:a aac -b:a 96k -shortest "${name}.mp4"
}
clip replay 8 magenta 1000
clip highlights 12 yellow 2000
ffmpeg -v error -y -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=10" -c:a pcm_s16le commentary.wav
ls -la
