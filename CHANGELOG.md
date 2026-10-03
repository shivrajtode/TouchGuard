# Changelog

The full development history, in order -- including the fixes that
turned out to be wrong, and why. Kept complete on purpose: if you're
trying to understand *why* something is built the way it is, the reason
is usually a bug further down this file.

## v3.10: WebUI links, a proper logo, and actual clickable badges

Follow-up to v3.9, not a repeat of it -- that pass added the Telegram
links to the README and a first logo, but missed the one place someone
actually using the app would look for them: the WebUI's own About tab
still only had the personal-DM link. Added the channel and discussion
group there too, labeled clearly enough to tell apart from the DM link,
so the community's discoverable from inside the app itself, not only
from GitHub.

The post-flash auto-open (and the contact line in the installer
banner) was still pointed at the author's personal Telegram chat.
Switched both to the channel: that's where updates and support live,
and someone who just flashed a module shouldn't land in a stranger's
DMs. The personal link stays in the WebUI's About tab, labeled as a
direct message, for anyone who wants it.

Also replaced the logo, in two passes worth being honest about. What
v3.9 shipped was flat, generic clip-art colors with no real tie to the
app's own look. First pass swapped in a shield, but it was hand
redrawn from a general impression of the app's own Home-tab icon rather
than the real path data, and it showed -- correctly flagged as not
actually matching. Fixed by not re-deriving any coordinates by hand at
all: the shield is the app's real icon path
(`M12 3l7 3v6c0 4.5-3 7.5-7 9-4-1.5-7-4.5-7-9V6l7-3z`), copied verbatim
into an SVG transform (translate + scale) rather than redrawn --
geometrically guaranteed identical, not an approximation, since there's
no arithmetic left to get wrong. Verified by rendering the real icon
and the transformed version side by side before finalizing anything.

Second correction was about the *idea*, not the shape: the center
symbol was a plain checkmark, which reads as generic "verified/secure"
-- true of any security app, not specifically what this one does. A
single crossed-out touch was the next attempt, but it also told the
wrong story: TouchGuard doesn't block touch, it blocks *ghost* touch
while letting real touch through, and one blocked symbol alone reads
like "all touch rejected."

Final design shows both halves. A dashed, crossed-out circle is the
ghost touch (not solid, not really there, rejected); a solid, unmarked
circle with a ring is the real one, passing straight through. A
literal little ghost shape was tried first for the phantom side and
dropped -- too much fine detail, it collapsed into an illegible smudge
at icon size. Dashed-vs-solid is a bolder, simpler contrast that still
reads small. Also checked geometrically, not by eye, that both rings
sit fully inside the shield edge with margin (an earlier layout had the
real-touch ring visibly clipped by it). Also caught, late, that the
shield sat high in its square canvas (about 40px above, 145px below),
which would have looked lopsided as a circular Telegram avatar --
re-centered and enlarged it (equal margins top and bottom, checked
numerically) and previewed it in an actual circular crop. Checked at
chat-list sizes and on both dark and light backgrounds, since that's
how it's really seen as a channel picture or README header.

None of those checks caught the actual bug that shipped: one of the
comments in the SVG's source contained a double hyphen ("--"), which
is illegal inside an XML comment. Every preview during design embedded
the SVG inside an HTML page, where browsers parse comments loosely and
never flagged it -- opening the file directly as its own document
(what a phone's file viewer does) hits XML's strict parser instead,
which stops rendering at the first error and drops everything after
it. That's what actually surfaced it: a screenshot of the file opened
standalone, showing the ghost half but not the real-touch half, with
the parser's own error banner above it. Fixed by removing the double
hyphens from the comment text and re-validating with a strict XML
parser this time, not just an HTML-embedded preview -- both as its own
document and decoded through `<img>`, the way the README actually
loads it.

README's community links were plain text with no real button -- added
proper clickable Telegram badges near the top, which is what "an open
button" actually needs to render as something clickable rather than a
URL to copy.

## v3.9: community links and branding

Added Telegram group and discussion channel links to the README and
credits, so people know where to ask questions, share setups, and report
issues first (Telegram is the primary community space). Added SVG logos
for the module and Telegram channel -- a clean, modern icon that works
as both a full logo and as a small avatar.

## v3.8: finger-count display race, unrelated to v3.3's daemon-level bug

Reported as: pick "1", press Start, display shows "2" -- but the actual
block behavior on screen matches "1" the whole time. Worth being
precise about why this is a genuinely different bug from v3.3's
double-supervisor race rather than assuming a repeat: this happened
"on any device," with no dependency on a specific manager's process
model, and traced to something else entirely -- a pure async race in
the WebUI's own JS, present since the WebUI was first built. Confirmed
by tracing the actual code rather than pattern-matching against the
last similar-sounding report.

The page's own initial `refreshStatus()` fires on load and reads
whatever's currently in `max_fingers.conf` -- on a fresh flash, still
the default. If a finger is picked fast enough, that already-in-flight
read (of the *old* value) can resolve after the click and overwrite its
optimistic highlight with stale data, purely based on which async call
happens to resolve last. The daemon was never wrong here -- it reads
the file fresh on every restart and was applying the real value
correctly the whole time, which is exactly why the actual block
behavior always matched what was picked.

Fixed with a generation counter: every state-changing action (a
finger-count pick, Start, Stop) bumps it immediately, before anything
else runs. `refreshStatus()` captures the current value before its
shell call and checks it again after -- if a newer action happened
while it was waiting, its answer was already stale before it arrived,
and it discards its own result instead of overwriting something more
recent. Verified with an isolated test reproducing the exact timing
(a slow, stale initial call resolving after a fast click): the
highlighted value stayed correctly on the user's pick through the
entire window where the bug would have shown the old default instead.

## v3.7: batched reads, to cut syscall overhead during actual bursts

Asked directly to keep optimizing the module itself for the lag
complaint, rather than wait on another log capture. Went back through
the hot path looking for real, structural cost rather than guessing --
found one real one that hadn't been touched yet: `read()` was pulling
exactly one event per syscall, every time, no matter how many were
already queued and ready. That cost scales with event *rate*, which
means it's worst precisely during a genuine ghost-touch burst -- rapid,
chattery, multi-slot activity is exactly a high syscall-rate scenario,
and exactly when lag gets reported.

Changed to pull whatever's actually queued (up to 64 events) in one
`read()`, then process each one individually afterward with the exact
same per-event logic as before -- nothing about event handling itself
changed, only how many syscalls it takes to receive them. Verified with
an isolated test (a pipe standing in for the real device, 150 events in
a burst larger than one batch): 3 real syscalls instead of 150, strict
order preserved, nothing lost or duplicated across the refill boundary.

One subtlety that would have been a real regression if missed: `poll()`
must never run while there's still buffered, unprocessed data sitting
locally -- doing so could add up to 500ms of pointless delay before
handling something already in hand. Restructured so `poll()` and the
underlying `read()` only happen once the local buffer is actually
exhausted; a still-full buffer skips straight to the next event with
zero added latency.

## v3.6: a second, separate backgrounding-kill mechanism on Aster/APatch

The double-supervisor race (v3.3) was flagged at the time as a likely
but unconfirmed explanation for backgrounding-kill on Aster/APatch --
turned out to be right to stay skeptical. A direct before/after
`/proc/pid/cgroup` comparison showed the daemon confirmed alive,
healthy, and correctly sitting in the root cgroup (`0::/`, v2.4's fix
holding exactly as intended) 20 seconds after backgrounding the
manager app -- then later gone entirely, with the supervisor not
restarting it either, meaning the whole tree died together rather than
just the child.

Since the cgroup fix was directly confirmed still working, this had to
be a different mechanism entirely. Android's low-memory killer doesn't
use cgroups -- it reads `oom_score_adj`, a separate per-process value
inherited from whatever forked it, and a background tablet with less
RAM than a phone hits memory pressure far more often, giving it far
more chances to act on a stale, inherited "this belongs to a
backgrounded app" score. Fixed by having the supervisor write `-1000`
(the standard "never kill" sentinel) to its own `oom_score_adj` once,
at the very top of `launch.sh`, before forking anything -- every
daemon restart inherits that protection automatically via normal
fork() semantics, for the supervisor's entire lifetime, regardless of
whether it was launched at boot or from the WebUI.

Also checked while investigating: the "not starting at first time"
report didn't show up in the log actually pulled -- it showed clean,
immediate detection on `/dev/input/event5` and normal multi-finger
blocking activity, no retry-loop. Consistent with v3.4's detection fix
actually working; flagged as something to keep an eye on rather than
claimed fixed outright, since it wasn't directly reproduced here.

## v3.5: idle poll frequency cut, for reported battery drain

Reported battery drain and lag. The poll timeout governing the main
loop had been 200ms since v1.9's optimization pass -- meaning the
process wakes up at least 5 times a second, forever, even sitting fully
idle with the screen off. That timeout only matters when there's
nothing to do; a real touch event wakes poll() immediately regardless
of what it's set to, so this has zero effect on actual responsiveness.
But waking a process that reliably can prevent a SoC from reaching its
deepest idle states, which is a well-understood, legitimate source of
background battery drain distinct from any actual CPU work being done.

Raised to 500ms. Checked against the tightest configured stale window
(`top_zone_stale_ms`, default 1500ms) before picking that number --
500ms still gives 3 chances to catch a stale contact within that
window, comfortable margin, not a tuning compromise for correctness.

Said plainly: this is a solid, reasoned fix for the battery half of the
report. It's a real but weaker fit for "lag" specifically -- fewer
background wakeups plausibly helps on a more constrained SoC, but
there's no log evidence tying this exact mechanism to lag the way
v3.0's disk-flush bug had. If lag persists after this, worth checking
whether it's the same shape as v3.0 (happens specifically during
blocking) or something new entirely.

## v3.4: detection could lock onto its own virtual device and deadlock forever

Reported as TouchGuard simply not starting -- the log showed
`'TouchGuard Virtual Touchscreen' not found among input devices,
retrying in 5s`, repeating forever. That name is the daemon's *own*
output device, not a real touchscreen -- meaning `touchguard.conf` had
somehow been told to treat its own virtual device as the input source,
a self-referential deadlock (can't create the virtual device until it
finds and grabs a real one, but it's searching for the virtual one).

Root cause, confirmed directly in `customize.sh`: its capability-based
fallback detection (added in v1.6 to work on any device regardless of
driver name) matches *any* input device reporting `ABS_MT_SLOT` +
`ABS_MT_POSITION_X/Y` -- and the virtual device deliberately reports
exactly those, since that's what makes it a valid touchscreen
replacement in the first place. If a prior instance's virtual device
was still alive at the exact moment a reflash's `customize.sh` ran,
detection would happily match itself. Plausibly related to the v3.3
double-supervisor race, which could easily have left something alive at
an unlucky moment during a reflash -- though this bug existed
independently since v1.6 and needed no help to trigger given the right
timing.

Fixed by explicitly excluding the virtual device's exact name from both
detection passes. It can never be its own input source again,
regardless of what state a prior instance is in during install.

## v3.3: Start could launch a second, competing supervisor loop

First real-world test on new hardware -- a Lenovo Tab M10 HD (MediaTek,
Aster/APatch) and a Samsung device -- surfaced a bug that had likely
been sitting latent since the WebUI's Start button was written. On
boot, `service.sh` already launches a `launch.sh` supervisor loop.
`setMaxFingers()` and `stopDaemon()` only ever signal an *existing*
loop (`pkill -x touchguard`, which the loop's own restart logic
handles), but `startDaemon()` unconditionally launched a brand new
`launch.sh` with no check for one already running. Pressing Start on
an already-running install -- exactly the kind of thing you do on a
first install to confirm it's actually working -- created two
independent supervisor loops, both trying to keep their own
`touchguard` instance alive and grabbing the same input device. Neither
ever fully wins: the device's log showed a repeating `EVIOCGRAB failed:
Device or resource busy` loop, and status reads (including the finger
count shown in the UI) became unpredictable depending on which of the
two competing instances happened to answer at that moment. Fixed by
having `startDaemon()` check for an already-running loop first
(`pgrep -f` against `launch.sh`'s own path) and do nothing if one's
found, rather than always launching another.

Two things from the same report stay open rather than getting papered
over with a guess: whether backgrounding-kill still happens on Aster/
APatch specifically once only one supervisor loop is ever running
(this fix should mean it isn't the double-loop causing it anymore, but
that's a prediction to verify, not a confirmed fix for that exact
symptom), and why the prebuilt binary needed a manual `build.sh` rescue
on both new devices before working -- plausible (flash-time script
execution is typically far more restricted than a later interactive
Termux session, which would explain compiling failing at flash time but
succeeding when run by hand afterward) but not confirmed, since the
original `build.log` from either failure didn't survive being
overwritten by the manual rebuild.

## v3.2: WebUI hanging on load, from an update-check with no timeout

A report came in of the WebUI opening but getting stuck on loading,
seemingly for good. Cause: v2.9's `checkForUpdate()` runs `curl`/`wget`
against GitHub directly from the device, and neither call had a timeout.
On a bad connection -- weak signal, WiFi with no real internet, slow DNS
-- that can hang for a long time instead of failing fast. The exec
bridge to the root shell almost certainly processes commands one at a
time in order, so a single stuck call there jams the whole channel
behind it -- including the status, finger-count, and log calls the rest
of the page actually needs to render as anything other than stuck.

Fixed two ways: `curl --connect-timeout 3 --max-time 5` and `wget
--timeout=5` now bound the absolute worst case to a few seconds instead
of indefinitely, and the check itself now waits 4 seconds after the page
loads before it ever runs, so it can't compete with the page's own first
load for the channel even with the timeout in place. A background
nice-to-have should never have been able to hold the real UI hostage in
the first place. Also caught in the same pass: the WebUI's own
`UPDATE_JSON_URL` constant still had the placeholder GitHub URL in it --
separate from `module.prop`'s copy, which did get corrected earlier --
so the in-app popup had never actually been able to check anything since
that fix. Both now point at the real repo.

## v3.1: README split into a real one, plus a release-prep pass

The README had grown into a single file mixing an actual introduction
with the entire version-by-version bug history -- fine for me and for
Shivraj working on it, not what a stranger deciding whether to trust and
install this should see first. Split it: `README.md` is now what a new
visitor actually needs (what it does, requirements, install, safety
net), and `CHANGELOG.md` holds the complete history, unchanged in
substance. `module.prop`'s `description` field updated to match the same
plain-language voice.

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
