#!/bin/bash
# The Good Egg demo re-cut at the captures' own 59.94 fps (the first cut was 30 fps), English labels.
set -e
cd /d/smg-ps2/videos
OUT=${1:-2026-10-16_demo-goodegg-60fps.mp4}
FONT="C\\:/Windows/Fonts/arialbd.ttf"
# file  start  length  crop (test builds: the debug lines off the top; TEASER builds: none)  label
SEGS=(
  "2026-10-16_0605_sleep-ps2_0247517.mp4 4.5 36 640:480:0:0 Opening, flight in and landing"
  "2026-10-16_0617_demo2-dino_7e3692c.mp4 17 32 576:432:32:48 Boss - Dino Piranha"
  "2026-10-16_2107_teaser-ota_688e72a.mp4 14.5 28.5 640:480:0:0 Boss - King Kaliente"
  "2026-10-16_2109_teaser-vine_688e72a.mp4 20.5 29.5 640:480:0:0 Climbing the vine"
  "2026-10-16_2127_teaser-fat_688e72a.mp4 27 38 640:480:0:0 The Hungry Luma"
)
TXT="fontfile='$FONT':fontcolor=white:borderw=3:bordercolor=black"
inputs=(-f lavfi -t 4 -i "color=black:s=960x720:r=60000/1001" -f lavfi -t 4 -i "anullsrc=r=48000:cl=stereo")
fc="[0:v]drawtext=$TXT:fontsize=64:text='Super Mario Galaxy':x=(w-tw)/2:y=250,"
fc+="drawtext=$TXT:fontsize=36:text='PlayStation 2 fan port':x=(w-tw)/2:y=350,"
fc+="drawtext=$TXT:fontsize=30:text='Good Egg Galaxy':x=(w-tw)/2:y=410,fade=in:0:30,fade=out:st=3.5:d=0.5,format=yuv420p[v0];[1:a]anull[a0];"
n=1
for s in "${SEGS[@]}"; do
  read -r f ss len crop label <<<"$s"
  inputs+=(-ss "$ss" -t "$len" -i "$f")
  i=$((n + 1))
  e=$(python -c "print($len - 0.4)")
  fc+="[$i:v]crop=$crop,scale=960:720:flags=lanczos,fps=60000/1001,setpts=PTS-STARTPTS,"
  fc+="drawtext=$TXT:fontsize=30:text='$label':x=24:y=h-th-24,fade=in:st=0:d=0.4,fade=out:st=$e:d=0.4,format=yuv420p[v$n];"
  fc+="[$i:a]aresample=48000,aformat=channel_layouts=stereo,asetpts=PTS-STARTPTS,afade=in:st=0:d=0.4,afade=out:st=$e:d=0.4[a$n];"
  n=$((n + 1))
done
for ((k = 0; k < n; k++)); do fc+="[v$k][a$k]"; done
fc+="concat=n=$n:v=1:a=1[v][a]"
ffmpeg -v error -y "${inputs[@]}" -filter_complex "$fc" -map "[v]" -map "[a]" \
  -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -r 60000/1001 -c:a aac -b:a 192k -movflags +faststart "$OUT"
ffprobe -v error -show_entries format=duration:stream=codec_type,r_frame_rate,width,height -of compact "$OUT"
