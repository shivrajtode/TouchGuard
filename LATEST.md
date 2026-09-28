## v3.10: WebUI links, a proper logo, and actual clickable badges

Follow-up to v3.9, not a repeat of it -- that pass added the Telegram
links to the README and a first logo, but missed the one place someone
actually using the app would look for them: the WebUI's own About tab
still only had the personal-DM link. Added the channel and discussion
group there too, labeled clearly enough to tell apart from the DM link,
so the community's discoverable from inside the app itself, not only
from GitHub.

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

README's community links were plain text with no real button -- added
proper clickable Telegram badges near the top, which is what "an open
button" actually needs to render as something clickable rather than a
URL to copy.



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

