# Changelog

The full development history, in order -- including the fixes that
turned out to be wrong, and why. Kept complete on purpose: if you're
trying to understand *why* something is built the way it is, the reason
is usually a bug further down this file.

## v3.0: fixed a real lag source, introduced by v2.8's own verbose mode

A report came in of the device lagging specifically during ghost-touch
blocking. Root cause, found by actually reading the hot path rather than
guessing: the log ring buffer's wrap-around handling called `fflush()`
-- a blocking disk write -- synchronously, inside the event loop, right
when it filled up. That threshold was ~8KB, which ordinary logging
(one line per whole block episode) was in practice never going to hit
mid-burst. v2.8's verbose mode changed that: it logs one line per
individual touch transition, and a genuine ghost-touch burst is exactly
rapid, chattery, multi-slot activity -- around 290 verbose lines was
enough to hit the wrap, and every wrap meant a real stall in real-time
input processing, at the exact moment it's under the most load. Not a
pre-existing bug; a regression from a debug feature added one version
ago, and worth being direct about that rather than filing it as
unexplained "lag."

Fixed by removing the synchronous flush entirely -- the ring now only
ever flushes on its existing 10-second timer or clean shutdown, both of
which were already time-bounded and never in the hot path. A burst that
somehow outpaces even that just loses its oldest buffered lines to being
overwritten, which is the right tradeoff: losing some diagnostic text
beats blocking input processing on disk I/O. Buffer size also raised
32KB for more headroom before that's ever relevant. Also dropped "zero
device lag" from the module description in `module.prop` -- true of the
core filtering logic in real testing, but not a claim to keep making
while a real lag report is still open, however it turns out.

## v2.9: quick actions + update checking -- built narrower than asked, on purpose

**Quick actions, not a terminal.** The ask was a freeform box that runs
any root command typed into it. Built instead: a fixed set of specific,
safe buttons (verbose logging on/off, restart the filter) that show the
exact command before/while it runs. A generic root-command box inside a
*published, community-facing* module is a different risk than the same
thing on one dev's own phone -- it's reachable by anyone who installs
this, at every skill level, and it invites exactly the "paste this
command, trust me" pattern that gets abused. `window.ksu.exec` already
only ever runs commands this file writes, never ones typed by whoever's
looking at the screen; that boundary is intentional and stays. For actual
freeform root access during development, Termux already does that job
without adding it to something everyone else installs too.

**Update checking, split into two parts.** `updateJson` in `module.prop`
now points to a manifest (standard Magisk/KernelSU schema: `version`,
`versionCode`, `zipUrl`, `changelog`) -- this is the same mechanism
LSPosed/Zygisk Next use, and it's what actually produces that polished
"update available" experience: it's the *root manager's own* native UI
reacting to this file, not something those projects built into their own
webview. Once hosted, most managers will surface it with zero extra work.

Layered on top: a WebUI banner that reads the same manifest and shows
version + a short summary, with an Update button and a dismissible close
(remembered per-version via `update_dismissed.conf`, so closing it
doesn't mean seeing it nag again for the same release). Deliberately
*not* built: the WebUI silently fetching and flashing the update itself.
That means root-level network calls on every launch before the user's
asked for anything, plus a hand-rolled download-unzip-overwrite-restart
pipeline with no real integrity checking, replacing files for a daemon
that exclusively grabs the touchscreen -- if that pipeline has a bug, the
failure mode is worse than most apps' update bugs. "Update" opens the
zip link through Android's normal download flow instead.

## v2.8: verbose diagnostic mode, for a rare intermittent stuck-touch report

A report came in of an occasional (not reproducible on demand) touch that
Android's own "Show touches" overlay shows as stuck, present with
TouchGuard installed and absent without it -- but not matching either the
v1.8 slot-desync signature (that fix is still unconditional and untouched
since v1.8, verified in the current source) or plain stale-timeout
behavior. Rather than theorize further at something this intermittent,
added an opt-in verbose mode (`verbose_log.conf`, default off) that logs
every real touch-down/touch-up read from the hardware, and every release
actually emitted to the virtual device, from all three places that can
emit one (normal forwarding, burst-block clear, stale-hide) -- tagged
consistently as `[v] real ...` / `[v] virtual release: ... (reason)`.

With this on, the next occurrence should be directly diagnosable from the
log alone: a `[v] real release` with no matching `[v] virtual release`
for the same slot pinpoints a genuine forwarding gap, precisely located,
without needing to catch it on screen recording.

## v2.7: live test pad + a catch counter -- with an honest limit on what the pad can show

Added a small multi-touch test area on Home, plus a running count of how
many burst-blocks and stale-hides have happened, read from the same log
the daemon already writes.

**What the test pad can't do, and why:** the original idea was a pad that
shows blocked touches turning red in real time. That's not actually
buildable, and it's worth explaining why rather than quietly shipping
something smaller without saying so: the daemon grabs the real
touchscreen device exclusively and only ever hands Android a filtered
stream through the virtual device it creates. A blocked ghost touch is
consumed at the raw kernel-event level and never becomes something any
app -- including this WebUI -- can receive. There is no "blocked" event
for a page to listen for.

What it does instead, honestly: every dot on the pad is a real touch that
already passed the filter, proving taps land exactly where you put them
with nothing added on top -- multi-finger included. The count sitting
right below it (`grep`-based, no daemon changes needed) is what represents
the invisible part: ghosts caught before they had a chance to reach this
far. Labeled "so far" rather than "today" -- the log has no per-line
timestamps (only the startup banner does), so a real calendar-day count
isn't accurate without changing what the daemon logs. "So far" is exactly
as accurate as what's actually measurable, and it already resets for free
whenever Clear is pressed on the Logs tab.

## v2.6: respects the OS-level "reduce motion" accessibility setting

All the spring/bounce motion and shape-morphing across every theme now
collapses to instant when the device has this turned on -- one global
override, not something that had to be handled per-theme or per-element.

## v2.5: WebUI rebuilt from scratch

Full rebuild, same daemon logic underneath (finger count, start/stop,
config paths -- none of it changed):

- **App-style navigation**, four sections instead of one long scroll:
  Home (finger count + start/stop, in that order), Logs, Theme, About.
- **Five selectable themes** (`theme.conf`, persists across sessions):
  Liquid Glass (the original translucent look), Material Expressive (bold
  tonal color, shapes that visibly change on selection, spring motion),
  Dark, Light, and System default (follows the phone's own setting live).
- **Logs**: Clear and Save-to-file (`/sdcard/Download/touchguard-log-<timestamp>.txt`)
  buttons, plus a plain-language error banner that appears specifically
  when the filter should be running but isn't -- distinct from the raw
  log output below it, which still shows the real error line.
- **About**: what it does and how to use it, in plain language, with
  author/Telegram info. No build/toolchain detail -- that's not
  something an end user installing a finished module needs to see.
- Fewer round trips to the root shell: status, finger count, and health
  checks that used to be 2+ separate `exec()` calls are now one.
- Removed the old busybox-httpd fallback WebUI (`webui.sh` + `webui/`)
  entirely -- pure dead weight, never used since the native `webroot/`
  WebUI took over early in this project.

## v2.4: the backgrounding kill, actually confirmed this time

v2.1 (`setsid`) and v2.2 (subshell reparent-to-init) both guessed at
process ancestry/session as the mechanism, and neither held. On-device
diagnostics (`/proc/<pid>/cgroup`) showed the real cause: the daemon was
still sitting inside the manager app's own cgroup v2 tracking slot
(`/apps/uid_<manager>/pid_<n>`), confirmed by `ps -A` coming back
completely empty right after backgrounding the app -- not frozen, killed
outright. Cgroup membership is inherited at fork time from whatever
spawned a process, and is a *completely separate axis* from parent-child
ancestry -- neither `setsid` nor reparenting ever touches it, which is
exactly why both previous attempts had no effect on this specific failure
mode, even though the reasoning behind each was sound for what it
actually addressed.

Fixed by explicitly moving the daemon into the root cgroup (checked at
`/sys/fs/cgroup/cgroup.procs`, falling back to `/dev/cg2_bpf/cgroup.procs`)
immediately after starting it, in both the boot-time start and the
WebUI's Start button. The root cgroup isn't tied to any app's lifecycle,
so it isn't a target for background-app cleanup regardless of what
mechanism Android/the ROM uses to enforce it. The subshell reparent-to-init
from v2.2 stays in place alongside this -- it's a real, separate protection
against ancestry-based cleanup, even though it wasn't the fix for this
particular symptom.

## v2.3: no more Termux/clang requirement for a fresh install

`prebuilt/touchguard-arm64` now ships in the zip -- a real compiled binary,
built via Termux/clang on this exact device and hardware (not something I
cross-compiled myself; I don't have an NDK or network access to fetch one).
`customize.sh` copies it into place and verifies it actually runs on the
installing device before trusting it (running with no arguments is a safe
no-op that either prints its usage line or fails immediately if the ABI
doesn't match), falling back to on-device compilation only if that check
fails. touchguard.c hardcodes nothing device-specific -- the touchscreen
path and axis ranges are both figured out at runtime -- so this one arm64
build should cover effectively any modern Android device, not just this
one. A fresh install no longer needs Termux or a compiler at all in the
common case.

## v2.2: touch stuck after screen wake, and the backgrounding fix wasn't enough

**Touch completely stuck after leaving the screen off for a while, rest of
the device fine (hardware buttons/UI still responsive), needing a reboot
to clear.** Different symptom shape from anything before -- not a ghost
contact, total unresponsiveness. Most touch controllers power down or
reinitialize when the screen sleeps; this daemon opens the device once at
startup and never revisits it, so if the driver comes back from that
suspend cycle in a state the held-open connection doesn't track correctly,
it can sit there producing nothing while still holding the exclusive grab
that would otherwise let Android fall back to the real device. Rather than
guess at the exact kernel-level cause, added a watchdog instead: probes
common backlight sysfs paths at startup, and if it detects the screen
turning on followed by a full grace period (`wake_grace_ms.conf`, default
6s) with zero real touch data despite that, treats its connection as
stale and exits cleanly -- reusing the already-hardened shutdown path --
so the supervisor brings up a fresh instance with a fresh open+grab within
about 2 seconds. No reboot needed. If no backlight path is found on a
given device, this just doesn't run; everything else is unaffected.

**The v2.1 backgrounding fix (`setsid`) wasn't sufficient on its own.**
`setsid` protects against session-based cleanup (e.g. a closed controlling
terminal) but not against ancestry-based cleanup -- if this manager keeps
a persistent root broker process alive rather than spawning a fresh `su
-c` per command (common, for performance), anything launched through it
remains a descendant of that broker regardless of its session, and stays
vulnerable if something kills the broker's whole process tree when the
app is backgrounded. Switched to `( setsid cmd & )` -- a subshell that
backgrounds the target and then has nothing left to do, so it exits
almost immediately, and the kernel reparents the now-orphaned process to
init (PID 1) right away. This removes the dependency on the broker's own
lifecycle entirely, rather than just changing which cleanup mechanism
might still catch it.

## v2.1: new ROM (InfinityX A16), new root manager -- three real issues found

1. **Daemon dying when the manager app is removed from background.** Both
   the boot-time start and the WebUI's Start button now launch the
   supervisor via `setsid` instead of plain `nohup ... &`. `setsid` puts it
   in a brand new session with no ties back to whatever process/session
   started it, so it can't get swept up if Android or the manager app
   decides to tear down the launching app's process tree. Falls back to
   plain `nohup` if this toybox build doesn't have `setsid`.

2. **Ghosts specifically in the top/notification area.** Added a
   configurable "top zone" (`top_zone_pct.conf`, default top 15% of the
   panel's Y range) with its own much shorter stale timeout
   (`top_zone_stale_ms.conf`, default 1500ms) that only applies to contacts
   that *started* in that region. A real interaction up there is almost
   always a quick tap or swipe, not a long motionless hold, so this doesn't
   affect genuine use -- it just means a ghost that likes to show up near
   the status bar gets caught in ~1-2s instead of waiting out the full
   30-second press-and-hold allowance the rest of the screen gets.

3. **Termux/clang dependency.** Cannot be removed entirely without shipping
   a precompiled binary, which needs an Android NDK/cross-compiler I don't
   currently have access to (no network to fetch one either). What *is*
   fixed: both `customize.sh` and `build.sh` now try to auto-install clang
   via Termux's own package manager if Termux is present but clang isn't --
   no more manual `pkg install clang` step.

## v2.0: press-and-hold, and the version number actually matches now

The stale-contact timeout (in place since v1.2, meant to auto-hide a ghost
that sits motionless forever) defaulted to 6 seconds -- which is also
exactly what a legitimate press-and-hold looks like, since both are just
"a contact that isn't moving." It was never actually a ghost-specific
signal, so it couldn't tell the two apart. Now that the real causes of a
truly-stuck contact are fixed at the source (v1.6's shutdown cleanup,
v1.8's slot-desync fix), this mechanism only needs to be a distant
backstop, not a fast trigger -- raised the default to 30 seconds, long
enough for any normal hold (long-press menus, hold-to-record, holding a
button) while still eventually recovering from something genuinely stuck
forever, without a reboot.

Also: the version number shown in the WebUI and in the install banner were
both separate hardcoded strings that had to be remembered and updated by
hand on every release -- which is exactly how the WebUI ended up showing
an old version after the last bump. Both now read the version live from
`module.prop` instead (the WebUI via the exec bridge, the installer via a
plain `grep`), so there's only one place version numbers are ever typed,
and the display can't go stale again.

Also added: an attempt to auto-open Telegram a few seconds after the
module finishes flashing. Magisk/KernelSU install scripts don't have a
fully standard way to launch another app from inside them, so this uses
`am start` from the root shell, delayed so it doesn't fight the flasher's
own UI -- it generally works on a booted device but isn't guaranteed
across every manager/ROM. The same link is always in the WebUI too.

## v1.9: zero-lag optimization pass

Previous versions worked correctly but added latency on this specific
hardware due to logging and stale-detection overhead in the event loop.
This version keeps all the bug fixes (slot desync, SYN_REPORT guarantee,
graceful shutdown) but moves performance-critical code paths into the
background:

- **Circular ring buffer for logs** -- no disk I/O in the hottest code
  path. Logs are accumulated in-memory and flushed every 10 seconds or
  when the ring wraps, never on demand. Eliminates the blocking
  `fprintf(stderr, ...)` syscall that was slowing event processing.
- **Lazy stale detection** -- instead of checking all 16 slots on every
  poll iteration, now runs every 100 events or every 5 seconds if the
  device is quiet. Keeps the nested slot loops out of the frame-by-frame
  path.
- **Reduced poll timeout** -- changed from 2000ms to 200ms so the stale
  timer gets a tighter window without spinning the CPU.
- **Optimized slot re-assertion** -- moved out of the loop condition to
  evaluate only once per frame instead of repeatedly.

(See v3.0 above: the "flush when the ring wraps" half of this eventually
needed a second pass once verbose logging could hit that wrap far more
often than anything here anticipated.)

## v1.8: the stuck touch that `ps` said was healthy the whole time

Confirmed via `ps` that the daemon never died or restarted -- yet the
stuck touch still happened. That ruled out every "device destroyed
mid-touch" theory (v1.6) and pointed at something that could corrupt
state while staying completely healthy: `release_all_slots()` (used by
the burst block, the stale-hide, and the failsafe -- i.e. constantly, on
this hardware) explicitly walks through and selects multiple slots on the
*virtual* device, one at a time. That leaves the virtual device's own
internal "current slot" pointer wherever that loop last left it --
independent of `cur_slot`, which is this program's tracking of the *real*
device's slot state. The multitouch protocol allows a frame to omit its
own slot selector when it hasn't changed since the last frame. If a real,
ongoing touch does exactly that right after one of those cleanups ran,
whatever got forwarded could land on the wrong slot on the virtual side --
while every internal check in the daemon itself stayed perfectly correct
throughout, which is exactly why `ps` showed nothing wrong.

Fixed by explicitly re-asserting the correct slot before forwarding any
frame, every time, rather than trusting whatever the virtual device
happens to still think is selected. Its slot state is now a direct
function of this program's own tracking, never a leftover from a cleanup
that ran moments earlier.

Also fixed in this pass: `pkill -f`/`pgrep -f` matched against the *full
command line* of every process, including whatever shell happened to be
running them -- a hot-patch script embedding `pkill -f touchguard/touchguard`
as literal text matched its own invoking shell and killed the update
mid-run. Switched to `-x touchguard` (exact short-name match), which can
never match a shell. And: the per-frame event buffer could silently drop
the terminating SYN_REPORT under a large enough burst (confirmed up to 10
simultaneous phantom contacts on this hardware) -- buffer raised to 512,
and a closing sync is now sent unconditionally regardless of buffer state.

## v1.6: the actual root cause of the stuck touches

Confirmed on a second device (a Lenovo tab with zero digitizer noise of its
own) that a touch could still get stuck there too -- which meant it was
never about ghost hardware at all. It was this: destroying the virtual
uinput device while a real touch was still down mid-relay. Any restart
(webui Stop, a finger-count change, a crash) killed the process, and
`do_cleanup()` tore down the virtual device without first telling Android
that touch had lifted. Android has no way to know the device is gone --
it just waits forever for an UP event from a device that no longer exists.
That's what actually needed a reboot to clear: not my daemon's state, the
window manager's.

Fixed by sending a full release for everything currently visible right
before the device is destroyed, on every exit path. Also: install is now
silent (only prints on an actual error), and touchscreen detection no
longer depends on recognizing a specific driver name -- it falls back to
pure capability detection (ABS_MT_SLOT + position axes), so a new device
type doesn't need a manual `touchguard.conf` edit the way the tablet did.

## v1.5: unconditional failsafe

The stale-contact detection in v1.2 assumed a stuck ghost sits still. If a
ghost instead drifts slowly -- more than `stale_px` of wander within any
`stale_ms` window -- it never qualifies as "stale" and can still wedge
blocking open forever, same end result as the original bug, different
trigger.

Rather than keep tuning that heuristic blind, v1.5 adds a hard ceiling:
if blocking has been active for more than `max_block_ms` (default 4000),
it gets force-cleared regardless of why it was stuck, and every contact
involved gets marked hidden so it can't immediately re-trigger the same
block. Worst case is now "touch hiccups for up to 4 seconds," not
"reboot required," no matter what causes it.

```
echo 3000 > /data/adb/modules/touchguard/max_block_ms.conf   # tighter ceiling
su -c "pkill -x touchguard"                                  # picks it up on restart
```

## v1.2: the bug that caused the reboots

The block/un-block logic used to clear only when the raw hardware finger
count hit exactly 0. If your panel has a contact that never sends a real
release -- the "one ghost touch stays" symptom -- then any burst that
happened while that contact was present could push the count up and it
would drop back down to 1 afterward, never 0. Blocking never cleared.
Every touch after that point was silently dropped until something killed
the process, which a reboot does.

Fixed two ways, together:
1. **Stale-contact detection.** Each contact's age and drift from its
   start position are tracked. Past `stale_ms` (default 6000, later
   raised to 30000 in v2.0) without moving more than `stale_px` (default
   15), it gets a synthetic release sent to the OS and is hidden going
   forward, even though the real hardware still thinks it's down.
2. **Blocking is based on visible contacts, not raw ones.** A hidden
   stale contact no longer counts toward "are fingers still down," for
   either triggering a block or clearing one. It can't wedge the block
   open anymore.

```
echo 8000 > /data/adb/modules/touchguard/stale_ms.conf   # milliseconds
echo 20   > /data/adb/modules/touchguard/stale_px.conf   # drift tolerance
su -c "pkill -x touchguard"                               # picks it up on restart
```
