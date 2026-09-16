#!/system/bin/sh
# Manual build, run from Termux if customize.sh couldn't compile at install
# time:
#
#   su -c "sh /data/adb/modules/touchguard/build.sh"
#
MODDIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$MODDIR/touchguard.c"
OUT="$MODDIR/touchguard"

# If Termux is present but clang isn't installed yet, try installing it
# automatically instead of requiring a manual `pkg install clang` step.
TERMUX_PREFIX="/data/data/com.termux/files/usr"
if [ ! -x "$TERMUX_PREFIX/bin/clang" ] && [ -x "$TERMUX_PREFIX/bin/apt" ]; then
  echo "clang not found, trying to install it via Termux..."
  HOME="/data/data/com.termux/files/home" \
  PREFIX="$TERMUX_PREFIX" \
  PATH="$TERMUX_PREFIX/bin:$PATH" \
  LD_LIBRARY_PATH="$TERMUX_PREFIX/lib" \
  "$TERMUX_PREFIX/bin/apt" install -y clang 2>&1 | tail -5
fi

CC=""
for c in \
  /data/data/com.termux/files/usr/bin/clang \
  /data/data/com.termux/files/usr/bin/gcc \
  clang cc gcc
do
  if [ -x "$c" ]; then CC="$c"; break; fi
  if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
done

if [ -z "$CC" ]; then
  echo "No compiler found."
  echo "In Termux: pkg install clang"
  echo "Then re-run this script."
  exit 1
fi

echo "Compiling with $CC ..."
if "$CC" -O2 -o "$OUT" "$SRC" 2>"$MODDIR/build.log"; then
  chmod 755 "$OUT"
  echo "Build OK -> $OUT"
else
  echo "Build FAILED, see $MODDIR/build.log"
  echo "--- last errors ---"
  tail -n 20 "$MODDIR/build.log"
  echo "-------------------"
  echo "If it mentions linux/uinput.h or linux/input.h not found,"
  echo "that's a missing kernel-header package in Termux, not a bug"
  echo "in the source -- tell me the exact error and I'll adjust it."
fi
