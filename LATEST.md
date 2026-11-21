## v3.13: when a touch gets stuck, TouchGuard stops for a few seconds and starts again

A touch that stays stuck for the time you set in Tune (the stuck-touch
timeout, or the shorter top-strip one) used to be hidden and nothing more.
Now the filter itself stops for 3 seconds and then starts again, which is
what a manual Stop and Start did for you. It only stops TouchGuard: the
touch panel is never reset and the screen is never touched. The length is
a new setting, "Pause when a touch is stuck" in Tune, from 1 to 30 seconds
or Off, and Off keeps the old hide-only behaviour.

How it works. The daemon hides the stuck touch as before, then leaves
through its normal cleanup (which releases the touchscreen and removes the
virtual device) with exit code 75, the same shape as the wake watchdog's
restart. The supervisor sees that code, keeps the filter off for the
configured seconds in one-second steps (so pressing Stop during a pause
still takes effect within a second), then starts it again. Any other exit
still restarts after 2 seconds. The Home screen shows "Paused for a
moment" instead of a crash warning, and the log says "Paused the filter
for 3 s".

A guard against flapping. A ghost that keeps coming back would otherwise
keep the filter off most of the time, so at most 3 pauses are allowed per
minute; after that a stuck touch is only hidden, as before, until the
minute clears. The history lives in a small file because every pause is a
new process; entries left over from before a reboot are ignored, and if
the file can't be written the guard allows the pause rather than blocking
it.

One thing that behaves as it always did and is easy to misread: fingers
inside a blocked ghost burst are still checked for being stuck. A burst
that lifts in time is only blocked; a cluster that sits still past the
stuck timeout counts as stuck, and now pauses the filter.

Heads up on the binary. This release changes the daemon, and the compiled
binary bundled in the zip is still the previous one, because it can't be
built on the machine this is developed on. The installer notices (the new
usage line lists "stuck-touch-pause") and prefers compiling the new source
when a compiler is already present; if there isn't one it keeps the old
binary, which works fine but cannot pause, and says so. Compiling is
`su -c "sh /data/adb/modules/touchguard/build.sh"` from Termux. The bundled
binary gets replaced once a build from a real device is confirmed.

Tested by running the real daemon against a faked kernel under
AddressSanitizer and UBSan: 22 scenarios (a stuck touch pauses with code
75 and cleans up in order, the third pause per minute is the last, Off
turns it off, a moving finger and a burst that lifts in time never pause,
a top-strip touch pauses on its own shorter timeout, bad settings fall
back to the default) plus 11 checks on the flap guard. The supervisor was
tested against a scripted daemon (19 checks: pause length, defaults and
clamping, marker file, Stop during a pause) and the installer's binary
selection across 7 situations. The control panel passed 85 behavior
checks, 35 layout checks and 16 polling, overflow and accessibility
checks. NOT yet run on a real device.
