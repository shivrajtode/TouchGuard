#!/system/bin/sh
# TouchGuard installer. Silent on a normal successful install -- only
# prints anything if something actually needs your attention.

FOUND_NAME=""
PATTERNS="nvt novatek goodix synaptics focaltech ft5 gt9 cyttsp atmel touchscreen touch_dev"

# Pass 1: prefer a device whose name matches a known touch-driver pattern
# AND reports real multitouch capability -- fastest path on common phones.
for dev in /sys/class/input/event*; do
  [ -f "$dev/device/name" ] || continue
  NAME="$(cat "$dev/device/name" 2>/dev/null)"
  [ -z "$NAME" ] && continue
  LOWER="$(echo "$NAME" | tr 'A-Z' 'a-z')"
  for p in $PATTERNS; do
    case "$LOWER" in
      *"$p"*)
        NODE="/dev/input/$(basename "$dev")"
        if getevent -pl "$NODE" 2>/dev/null | grep -q ABS_MT_SLOT; then
          FOUND_NAME="$NAME"
        fi
        ;;
    esac
    [ -n "$FOUND_NAME" ] && break
  done
  [ -n "$FOUND_NAME" ] && break
done

# Pass 2: nothing matched a known name (different vendor/driver -- e.g. a
# tablet with its own panel). Fall back to pure capability detection: any
# device reporting the defining multitouch axes IS a touchscreen, whatever
# it's called. This is what makes install work unmodified across devices.
if [ -z "$FOUND_NAME" ]; then
  for dev in /sys/class/input/event*; do
    [ -f "$dev/device/name" ] || continue
    NAME="$(cat "$dev/device/name" 2>/dev/null)"
    [ -z "$NAME" ] && continue
    NODE="/dev/input/$(basename "$dev")"
    CAPS="$(getevent -pl "$NODE" 2>/dev/null)"
    if echo "$CAPS" | grep -q ABS_MT_SLOT \
       && echo "$CAPS" | grep -q ABS_MT_POSITION_X \
       && echo "$CAPS" | grep -q ABS_MT_POSITION_Y; then
      FOUND_NAME="$NAME"
      break
    fi
  done
fi

if [ -z "$FOUND_NAME" ]; then
  ui_print "TouchGuard: could not auto-detect a touchscreen device."
  ui_print "  Run: su -c 'cat /sys/class/input/event*/device/name'"
  ui_print "  then set TS_NAME in $MODPATH/touchguard.conf by hand."
  FOUND_NAME="UNKNOWN"
fi

echo "TS_NAME=\"$FOUND_NAME\"" > "$MODPATH/touchguard.conf"

# Precompiled binary first (when one is bundled in the zip) -- no compiler
# needed at all for the common case. touchguard.c hardcodes nothing
# device-specific (touchscreen path and axis ranges are both figured out
# at runtime), and the ioctl/libc surface it uses is stable across arm64
# Android devices, so one build covers effectively all of them. Verified
# before being trusted: running it with no arguments is a safe no-op that
# prints a usage line if the binary is valid for this device's
# architecture, or fails immediately (wrong ABI, corrupted file) if not --
# either way this costs nothing and can't leave a broken binary in place.
PREBUILT_OK=0
if [ -f "$MODPATH/prebuilt/touchguard-arm64" ] && [ "$(uname -m)" = "aarch64" ]; then
  cp -f "$MODPATH/prebuilt/touchguard-arm64" "$MODPATH/touchguard"
  chmod 755 "$MODPATH/touchguard"
  if "$MODPATH/touchguard" 2>&1 | grep -q "usage:"; then
    PREBUILT_OK=1
  else
    rm -f "$MODPATH/touchguard"
  fi
fi

if [ "$PREBUILT_OK" = "0" ]; then
  # No usable precompiled binary for this device -- compile from source.
  # If Termux is present but clang isn't installed yet, try installing it
  # automatically instead of requiring a manual `pkg install clang` step.
  TERMUX_PREFIX="/data/data/com.termux/files/usr"
  if [ ! -x "$TERMUX_PREFIX/bin/clang" ] && [ -x "$TERMUX_PREFIX/bin/apt" ]; then
    HOME="/data/data/com.termux/files/home" \
    PREFIX="$TERMUX_PREFIX" \
    PATH="$TERMUX_PREFIX/bin:$PATH" \
    LD_LIBRARY_PATH="$TERMUX_PREFIX/lib" \
    "$TERMUX_PREFIX/bin/apt" install -y clang >/dev/null 2>&1
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

  if [ -n "$CC" ]; then
    if "$CC" -O2 -o "$MODPATH/touchguard" "$MODPATH/touchguard.c" 2>"$MODPATH/build.log"; then
      chmod 755 "$MODPATH/touchguard"
    else
      ui_print "TouchGuard: build FAILED -- see build.log in the module folder."
      ui_print "  Fix: from Termux run su -c 'sh $MODPATH/build.sh'"
    fi
  else
    ui_print "TouchGuard: no precompiled binary for this device, and no C"
    ui_print "  compiler found either. From Termux: pkg install clang"
    ui_print "  then: su -c 'sh $MODPATH/build.sh'"
  fi
fi

chmod 755 "$MODPATH/build.sh" "$MODPATH/launch.sh" "$MODPATH/service.sh" "$MODPATH/uninstall.sh" 2>/dev/null
chmod -R 755 "$MODPATH/webroot" 2>/dev/null

[ -f "$MODPATH/max_fingers.conf" ]  || echo "2"    > "$MODPATH/max_fingers.conf"
[ -f "$MODPATH/stale_ms.conf" ]     || echo "30000" > "$MODPATH/stale_ms.conf"
[ -f "$MODPATH/stale_px.conf" ]     || echo "15"   > "$MODPATH/stale_px.conf"
[ -f "$MODPATH/max_block_ms.conf" ] || echo "4000" > "$MODPATH/max_block_ms.conf"
[ -f "$MODPATH/top_zone_pct.conf" ]      || echo "15"   > "$MODPATH/top_zone_pct.conf"
[ -f "$MODPATH/top_zone_stale_ms.conf" ] || echo "1500" > "$MODPATH/top_zone_stale_ms.conf"
[ -f "$MODPATH/wake_grace_ms.conf" ]     || echo "6000" > "$MODPATH/wake_grace_ms.conf"
[ -f "$MODPATH/theme.conf" ]             || echo "glass" > "$MODPATH/theme.conf"
[ -f "$MODPATH/verbose_log.conf" ]       || echo "0" > "$MODPATH/verbose_log.conf"
[ -f "$MODPATH/update_dismissed.conf" ]  || echo "0" > "$MODPATH/update_dismissed.conf"

# Post-install message. Version is read from module.prop rather than
# hardcoded here -- this file drifting out of sync with module.prop is
# exactly what caused the WebUI to show a stale version number before.
TG_VERSION="$(grep '^version=' "$MODPATH/module.prop" | cut -d= -f2)"
ui_print ""
ui_print "╔════════════════════════════════════════════════════════════════╗"
ui_print "║              TouchGuard $TG_VERSION Installed                            ║"
ui_print "╠════════════════════════════════════════════════════════════════╣"
ui_print "║                                                                ║"
ui_print "║ ✓ No Termux/compiler needed for a fresh install               ║"
ui_print "║ ✓ Auto-recovers if the touch driver doesn't resume cleanly     ║"
ui_print "║ ✓ Survives the manager app being killed in the background      ║"
ui_print "║                                                                ║"
ui_print "║ Module by: Shivraj (@Shivraj_editx)                           ║"
ui_print "║ Message me on Telegram: https://t.me/Shivraj_editx            ║"
ui_print "║                                                                ║"
ui_print "║ Open TouchGuard in KSU manager to view the live log viewer     ║"
ui_print "║ and control the filter daemon without rebooting.              ║"
ui_print "║                                                                ║"
ui_print "╚════════════════════════════════════════════════════════════════╝"
ui_print ""

# Auto-open Telegram a few seconds after flashing finishes. Backgrounded
# and delayed so it doesn't fight the flasher's own UI while it's still
# showing progress -- fires once the manager app would reasonably have
# returned control to you. This depends on `am` working from a root shell
# outside a normal app context, which generally works on a booted device
# but isn't a fully guaranteed path across every ROM/manager combination --
# the same link is always available in the WebUI as a manual fallback.
(sleep 4; am start -a android.intent.action.VIEW -d "https://t.me/Shivraj_editx" >/dev/null 2>&1) &
