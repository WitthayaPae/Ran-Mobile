#!/bin/bash
cd "$(dirname "$0")"
./build.sh "${1:-Lib_Engine}" > /dev/null 2>&1
LOG=out/arm64-v8a/build.log
grep -vE '^C:\PROGRA|^\[|^FAILED|^ninja' "$LOG" > out/arm64-v8a/err.log
echo "errors: $(grep -c 'error:' $LOG)  failed TUs: $(grep -c '^FAILED' $LOG)"
grep -oE "error: .*" out/arm64-v8a/err.log | sed "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -${2:-25}
