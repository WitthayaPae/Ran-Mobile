#!/bin/bash
# Emit a probe TU that takes sizeof() of every message struct so clang is forced
# to lay each one out (a plain #include leaves most records un-laid-out).
SRC="$(cd "$(dirname "$0")/../../../SOURCE" && pwd)"
G="$SRC/Lib_Client/G-Logic"
OUT="$(dirname "$0")/probe.cpp"
{
  echo '#include "stdafx.h"'
  echo '#include "GLContrlInvenMsg.h"'   # holds enums the other message headers use
  for h in $(ls "$G" | grep -E '^GLContrl.*\.h$'); do echo "#include \"$h\""; done
  echo
  echo '// force layout of every message struct'
  echo 'template <class T> struct RanSize { static const size_t value = sizeof(T); };'
  echo 'size_t ran_layout_probe_total = 0;'
  echo 'using namespace GLMSG;'
  echo 'void ran_layout_probe() {'
  for h in $(ls "$G" | grep -E '^GLContrl.*\.h$'); do
    awk '/^[ \t]*struct[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*(:|$|\{)/ {
           n=$2; gsub(/[^A-Za-z0-9_]/,"",n); if (n!="") print n }' "$G/$h"
 sort -u | sort -u | while read n; do
    echo "    ran_layout_probe_total += RanSize<$n>::value;"
  done
  echo '}'
} > "$OUT"
grep -c "RanSize<" "$OUT"
