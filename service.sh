#!/system/bin/sh
# Runs late at boot. Only starts anything if the "enabled" flag file exists
# in the module folder -- customize.sh deliberately does not create it.
MODDIR="$(cd "$(dirname "$0")" && pwd)"

[ -f "$MODDIR/enabled" ] || exit 0

# The actual reason the daemon was dying when the manager app got removed
# from background: cgroup v2 membership, not process ancestry. A process
# spawned through the manager's root bridge inherits cgroup membership
# from whatever spawned it -- which traces back to the manager app's own
# tracked cgroup slot (confirmed on-device: /apps/uid_<manager>/pid_<n>).
# setsid and reparent-to-init both operate on session/ancestry, neither of
# which touches cgroup membership at all, so neither one actually helped.
# When Android kills that app's cgroup on background removal, everything
# still sitting in it goes too, root or not. Fix: explicitly move into the
# root cgroup (not tied to any app's lifecycle) right after starting.
CGROUP_ROOT=""
for c in /sys/fs/cgroup/cgroup.procs /dev/cg2_bpf/cgroup.procs; do
  [ -w "$c" ] && CGROUP_ROOT="$c" && break
done

start_detached() {
  # ( cmd & ) -- subshell backgrounds cmd then exits immediately, so the
  # kernel reparents the now-orphaned process to init (PID 1) right away.
  # Still worth keeping alongside the cgroup fix: ancestry-based cleanup
  # is a real, separate thing this also guards against.
  (
    if command -v setsid >/dev/null 2>&1; then
      setsid sh "$1" >/dev/null 2>&1 &
    else
      sh "$1" >/dev/null 2>&1 &
    fi
    CPID=$!
    if [ -n "$CGROUP_ROOT" ]; then
      echo "$CPID" > "$CGROUP_ROOT" 2>/dev/null
    fi
  )
}

start_detached "$MODDIR/launch.sh"
