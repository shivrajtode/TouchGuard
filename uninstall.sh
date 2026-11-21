#!/system/bin/sh
# Grab + uinput device are tied to the daemon's own file descriptors, so
# killing it is enough -- the kernel releases the grab automatically.
pkill -x touchguard 2>/dev/null
# Scratch files the stuck-touch pause leaves behind (the pause marker and its flap-guard history).
rm -f /data/local/tmp/touchguard_paused /data/local/tmp/touchguard_pause_state 2>/dev/null
