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

