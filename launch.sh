#!/system/bin/sh
# Resolves the touchscreen's current /dev/input/eventN (numbers can shift
# between boots) and supervises the daemon, restarting it if it exits,
# until the disable flag shows up.
MODDIR="$(cd "$(dirname "$0")" && pwd)"
CONF="$MODDIR/touchguard.conf"
BIN="$MODDIR/touchguard"
LOG="$MODDIR/touchguard.log"
DISABLE_FLAG="/data/local/tmp/touchguard_disable"

: > "$LOG"

if [ ! -x "$BIN" ]; then
  echo "$(date): binary missing/not executable, run build.sh first" >> "$LOG"
  exit 0
fi
if [ ! -f "$CONF" ]; then
  echo "$(date): touchguard.conf missing" >> "$LOG"
  exit 0
fi

. "$CONF"

if [ "$TS_NAME" = "UNKNOWN" ] || [ -z "$TS_NAME" ]; then
  echo "$(date): TS_NAME not resolved, edit $CONF and set it manually" >> "$LOG"
  exit 0
fi

while [ ! -f "$DISABLE_FLAG" ]; do
  EVDEV=""
  for n in /sys/class/input/event*; do
    NAME="$(cat "$n/device/name" 2>/dev/null)"
    if [ "$NAME" = "$TS_NAME" ]; then
      EVDEV="/dev/input/$(basename "$n")"
      break
    fi
  done

  if [ -z "$EVDEV" ]; then
    echo "$(date): '$TS_NAME' not found among input devices, retrying in 5s" >> "$LOG"
    sleep 5
    continue
  fi

  echo "$(date): starting touchguard on $EVDEV" >> "$LOG"
  "$BIN" "$EVDEV" >> "$LOG" 2>&1
  echo "$(date): daemon exited, restarting in 2s" >> "$LOG"
  sleep 2
done

echo "$(date): disable flag found, stopping supervisor" >> "$LOG"
