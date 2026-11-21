#!/system/bin/sh
# Resolves the touchscreen's current /dev/input/eventN (numbers can shift
# between boots) and supervises the daemon, restarting it if it exits,
# until the disable flag shows up.
#
# Exit code 75 from the daemon means "I stopped on purpose because a touch was
# stuck": the filter stays off for the configured number of seconds
# (stale_pause_s.conf, default 3) and then starts again. Any other exit is a
# normal restart after 2 s. TG_TMPDIR / TG_SYS_INPUT exist only so this script
# can be tested off a phone; the defaults are the real paths.
MODDIR="$(cd "$(dirname "$0")" && pwd)"
CONF="$MODDIR/touchguard.conf"
BIN="$MODDIR/touchguard"
LOG="$MODDIR/touchguard.log"
TMPD="${TG_TMPDIR:-/data/local/tmp}"
SYS_INPUT="${TG_SYS_INPUT:-/sys/class/input}"
DISABLE_FLAG="$TMPD/touchguard_disable"
PAUSED_FLAG="$TMPD/touchguard_paused"   # exists only while the filter is deliberately paused (the WebUI reads it)

# Tell the OOM killer to never touch this process, regardless of what app
# forked it or what cgroup it's in -- oom_score_adj is a completely separate
# mechanism from the cgroup migration done elsewhere. Written once, here,
# before anything is forked: children inherit the parent's oom_score_adj at
# fork() time, so this protects every daemon restart for the supervisor's
# whole lifetime, not just this one process.
echo -1000 > /proc/$$/oom_score_adj 2>/dev/null

: > "$LOG"
rm -f "$PAUSED_FLAG"

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
  for n in "$SYS_INPUT"/event*; do
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
  RC=$?
  if [ "$RC" -eq 75 ]; then
    PAUSE_S="$(cat "$MODDIR/stale_pause_s.conf" 2>/dev/null)"
    case "$PAUSE_S" in ''|*[!0-9]*|?????*) PAUSE_S=3 ;; esac    # not a short plain number: use the default
    PAUSE_S=$(expr "$PAUSE_S" + 0)                              # "07" -> 7 (and never read as octal)
    [ "$PAUSE_S" -lt 1 ] && PAUSE_S=1     # the daemon only exits 75 when its own setting was above 0
    [ "$PAUSE_S" -gt 30 ] && PAUSE_S=3    # out of range: the daemon falls back to its default, so do the same
    : > "$PAUSED_FLAG"
    echo "$(date): stuck touch, filter paused for ${PAUSE_S}s, restarting after that" >> "$LOG"
    # One-second steps, so pressing Stop during a pause is honoured within a second.
    i=0
    while [ "$i" -lt "$PAUSE_S" ] && [ ! -f "$DISABLE_FLAG" ]; do sleep 1; i=$((i + 1)); done
    rm -f "$PAUSED_FLAG"
  else
    echo "$(date): daemon exited, restarting in 2s" >> "$LOG"
    sleep 2
  fi
done

rm -f "$PAUSED_FLAG"

echo "$(date): disable flag found, stopping supervisor" >> "$LOG"
