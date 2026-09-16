#!/system/bin/sh
# Grab + uinput device are tied to the daemon's own file descriptors, so
# killing it is enough -- the kernel releases the grab automatically.
pkill -x touchguard 2>/dev/null
