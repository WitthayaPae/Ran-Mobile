#!/system/bin/sh
#
#  Android runs this instead of launching the app directly, for a debuggable
#  APK that ships it in lib/<abi>/. It is how AddressSanitizer gets in without
#  root: the runtime has to be loaded before libc hands out the first block.
#
#  Only present in an ASAN=1 build - see build-apk.sh.
#
HERE="$(dirname "$0")"

#  Proof that Android ran this at all.
echo "wrapran $(date)" > /sdcard/ran/wrapran.txt 2>/dev/null
export LD_PRELOAD="$HERE/libclang_rt.asan-aarch64-android.so"

#  What the report should contain, and what should not stop the run.
#
#  A heap-buffer-overflow or a use-after-free is the thing being hunted, so
#  those abort with a full backtrace. Leaks are not: this client leaks by
#  design in places and a leak report at exit would bury the signal.
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:alloc_dealloc_mismatch=0:halt_on_error=1:print_stats=0:log_to_syslog=1

exec "$@"
