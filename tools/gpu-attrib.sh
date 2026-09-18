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
}
trap restore EXIT INT TERM

"$HERE/ios-device.sh" flag noheatpace >/dev/null 2>&1 \
  && echo "noheatpace set - the frame rate is held for the sweep" \
  || echo "WARNING: could not set noheatpace; a thermal clamp mid-sweep will fake large costs"

#  Average Device Utilisation over SAMPLE readings. dvt emits one a second.
gpu() {
  timeout $((SAMPLE + 20)) "$PY" -m pymobiledevice3 developer dvt graphics 2>/dev/null \
    | grep -oE '"Device Utilization %": [0-9]+' | grep -oE '[0-9]+$' \
    | head -"$SAMPLE" \
    | awk '{s+=$1;n++} END {if(n) printf "%.0f", s/n; else printf "?"}'
}

echo "settle ${SETTLE}s, average ${SAMPLE} samples per section"
echo

restore; sleep "$SETTLE"
BASE=$(gpu)
printf '  %-18s %4s%%\n' "(baseline)" "$BASE"
echo

for S in "${SECTIONS[@]}"; do
  "$HERE/ios-device.sh" flag sectionskip "$S" >/dev/null 2>&1
  sleep "$SETTLE"
  V=$(gpu)
  if [ "$V" = "?" ] || [ "$BASE" = "?" ]; then
    printf '  %-18s %4s%%   (no reading)\n' "$S" "$V"
  else
    printf '  %-18s %4s%%   -%s points\n' "$S" "$V" "$((BASE - V))"
  fi
done

echo
echo "baseline $BASE% - a section's points are what the GPU stopped doing without it."
