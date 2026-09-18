#!/bin/bash
#  What is the GPU actually spending its time on?
#
#  The frame time cannot answer this. On an iPhone 15 in a 242-player crowd the
#  client holds 60 fps with the CPU half idle and the GPU pinned at 82% - the
#  heat is the GPU load, and the frame time says nothing about it. So: drop one
#  frame section at a time and ask the GPU how much it stopped doing.
#
#      ./tools/gpu-attrib.sh                 # the standard sweep
#      ./tools/gpu-attrib.sh eff:afterrender eff:tree-after
#      SETTLE=20 SAMPLE=30 ./tools/gpu-attrib.sh
#
#  The picture is wrong while this runs - that is the point of it. Everything is
#  put back at the end, including on Ctrl-C.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PY="/c/Users/tapnu/AppData/Local/Programs/Python/Python312/python.exe"

SETTLE="${SETTLE:-14}"     # seconds to let the phone settle after a change
SAMPLE="${SAMPLE:-18}"     # samples of GPU utilisation to average

#  The default sweep: the four top-level sections that were measured first, then
#  the eight passes world-eff turned out to be, then the two halves of the name
#  render. Override by passing section names as arguments.
SECTIONS=( "$@" )
if [ ${#SECTIONS[@]} -eq 0 ]; then
  SECTIONS=( interface w:mobitem w:land world-eff \
             eff:animan eff:alphamap eff:tree-after eff:tree-after1 \
             eff:afterrender eff:alphapiece eff:landeff eff:weather \
             ui:names-plate ui:names-text )
fi

#  Hold the frame rate for the duration.
#
#  A section's cost is the drop in GPU utilisation when it stops being drawn,
#  and that only compares if every reading was taken at the same frame rate.
#  The first real sweep crossed the thermal threshold four minutes in, the clamp
#  halved the display rate, and every section after that read as costing ~33
#  points - children of world-eff apparently costing three times their parent.
#  So the sweep sets noheatpace itself rather than trusting whoever runs it to
#  remember, and clears it again at the end.
restore() {
  "$HERE/ios-device.sh" unflag sectionskip  >/dev/null 2>&1 || true
  "$HERE/ios-device.sh" unflag noheatpace   >/dev/null 2>&1 || true
  [ -n "${FPSPID:-}" ] && kill "$FPSPID" 2>/dev/null
  [ -n "${FPSLOG:-}" ] && rm -f "$FPSLOG"
  return 0
}
trap restore EXIT INT TERM

#  HOLD=0 leaves the thermal clamp alone and measures at whatever rate the
#  phone is actually holding. Once a phone is hot enough it cannot sustain 60
#  in a crowd even with the clamp lifted - it throttles and drifts, and the
#  readings stop comparing. A steady clamped 30 is worth more than an unsteady
#  60: the costs come out roughly halved, but they come out.
if [ "${HOLD:-1}" = 0 ]; then
  echo "HOLD=0 - measuring at the phone own rate, clamp left alone"
else
  "$HERE/ios-device.sh" flag noheatpace >/dev/null 2>&1 \
    && echo "noheatpace set - the frame rate is held for the sweep" \
    || echo "WARNING: could not set noheatpace; a clamp mid-sweep will fake large costs"
fi

#  Read the frame rate back, per reading, and print it beside the GPU figure.
#
#  Setting noheatpace is not enough on its own. It has been wrong twice: once
#  the clamp engaged half way through and halved the rate, and once the rate
#  climbed from 30 to 60 during the baseline itself, so every later section
#  measured against a baseline taken at the wrong rate and came out NEGATIVE -
#  a section whose removal made the GPU busier. Both were caught by hand,
#  afterwards. The tool should catch them.
FPSLOG="$(mktemp -t ranfps.XXXXXX)"
"$HERE/ios-device.sh" log > "$FPSLOG" 2>&1 &
FPSPID=$!

#  The mean frame rate over the lines logged since the marker this writes.
fps_since() {
  awk -v start="$1" '
    /FRAME [0-9.]+ fps/ && NR > start {
      if (match($0, /FRAME [0-9.]+ fps/)) {
        s = substr($0, RSTART + 6, RLENGTH - 10); t += s; n++
      }
    }
    END { if (n) printf "%.0f", t / n; else printf "?" }' "$FPSLOG"
}
fps_mark() { wc -l < "$FPSLOG" 2>/dev/null || echo 0; }

#  Average Device Utilisation over SAMPLE readings. dvt emits one a second.
gpu() {
  timeout $((SAMPLE + 20)) "$PY" -m pymobiledevice3 developer dvt graphics 2>/dev/null \
    | grep -oE '"Device Utilization %": [0-9]+' | grep -oE '[0-9]+$' \
    | head -"$SAMPLE" \
    | awk '{s+=$1;n++} END {if(n) printf "%.0f", s/n; else printf "?"}'
}

echo "settle ${SETTLE}s, average ${SAMPLE} samples per section"
echo

#  The baseline waits twice: once for the rate to settle after noheatpace lifts
#  the clamp, and once more to measure it. A baseline taken while the rate is
#  still climbing poisons every reading that follows.
"$HERE/ios-device.sh" unflag sectionskip >/dev/null 2>&1 || true
sleep "$SETTLE"
M=$(fps_mark); sleep "$SETTLE"; BASEFPS=$(fps_since "$M")
M=$(fps_mark); BASE=$(gpu); BASEFPS2=$(fps_since "$M")
printf '  %-18s %4s%%   %s fps\n' "(baseline)" "$BASE" "$BASEFPS2"
if [ "$BASEFPS" != "$BASEFPS2" ]; then
  echo
  echo "  STOP: the frame rate moved during the baseline ($BASEFPS -> $BASEFPS2 fps)."
  echo "  Every reading would be measured against the wrong one. Let the phone"
  echo "  settle and run it again."
  exit 1
fi
echo

BAD=0
for S in "${SECTIONS[@]}"; do
  "$HERE/ios-device.sh" flag sectionskip "$S" >/dev/null 2>&1
  sleep "$SETTLE"
  M=$(fps_mark); V=$(gpu); F=$(fps_since "$M")
  if [ "$V" = "?" ] || [ "$BASE" = "?" ]; then
    printf '  %-18s %4s%%   %-3s fps  (no reading)\n' "$S" "$V" "$F"
  elif [ "$F" != "$BASEFPS2" ]; then
    #  Not a cost. The frame rate is the variable, not the section.
    printf '  %-18s %4s%%   %-3s fps  INVALID - rate differs from baseline %s\n' \
           "$S" "$V" "$F" "$BASEFPS2"
    BAD=$((BAD + 1))
  else
    printf '  %-18s %4s%%   %-3s fps  %s points\n' "$S" "$V" "$F" "$((BASE - V))"
  fi
done

echo
echo "baseline $BASE% at $BASEFPS2 fps - a section's points are what the GPU stopped doing without it."
[ "$BAD" -gt 0 ] && echo "$BAD reading(s) INVALID: taken at a different frame rate, so the difference is not a cost."
exit 0
