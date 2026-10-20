#!/bin/sh
# Front-end smoke test: play scripted games without a window (new game, loaded games) and
# check that the program neither crashes nor hangs.
set -e
dir=$(mktemp -d)
keys() {
    k="$1"
    i=0
    while [ $i -lt "$2" ]; do k="$k SPACE ENTER"; i=$((i + 1)); done
    echo "$k SHOT:$dir/end.bmp"
}
run() {
    CIV_HEADLESS=1 CIV_KEYS="$2" timeout 900 ./civ --saves "$dir" 2>"$dir/log" || { echo "FAIL $1"; cat "$dir/log"; exit 1; }
    [ -f "$dir/end.bmp" ] || { echo "FAIL $1 (script not finished)"; exit 1; }
    rm -f "$dir/end.bmp"
    turns=$(sed -n 's/^game ended: turn \([0-9]*\).*/\1/p' "$dir/log" | head -1)
    echo "PASS $1 (first game ended on turn $turns)"
}
run "new game" "$(keys 'ENTER ENTER ENTER ENTER ESC ENTER ENTER ENTER b ENTER ENTER ENTER' 800)"
for slot in 0 2 4 9; do
    k="2 ENTER ENTER"                 # Load a Saved Game, the drive page
    i=0
    while [ $i -lt $slot ]; do k="$k 2"; i=$((i + 1)); done
    run "saved game $slot" "$(keys "$k ENTER" 6000)"
done
# sound: a theme and an effect through SDL's audio (to a file with the disk driver) must
# come out as the recorded square wave, not silence
for n in 19 44; do
    SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$dir/s.raw" SDL_DISKAUDIODELAY=0 timeout 60 ./civ --sound-test $n \
        2>/dev/null || { echo "FAIL sound $n"; exit 1; }
    python3 -I -c "import sys,struct; d=open(sys.argv[1],'rb').read(); s=struct.unpack('<%dh'%(len(d)//2),d); \
n=sum(1 for i in range(1,len(s)) if s[i-1]<0<=s[i]); sys.exit(0 if n>100 else 1)" "$dir/s.raw" \
        && echo "PASS sound $n" || { echo "FAIL sound $n (silent)"; exit 1; }
    rm -f "$dir/s.raw"
done
rm -rf "$dir"
