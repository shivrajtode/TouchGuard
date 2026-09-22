# TouchGuard

**Stops phantom touches on your screen — without you feeling a thing.**

Sometimes an Android touchscreen reports touches that never actually
happened: several fingers appearing out of nowhere, or one spot that
gets stuck and won't let go, usually needing a reboot to clear.
TouchGuard catches these the instant they happen and quietly discards
them before your apps ever see them. Everything you actually do with
your own fingers — tapping, swiping, holding, multi-finger gestures —
passes through exactly as normal, completely untouched.

No background service draining your battery, no accessibility
permission watching what you do. It works at the lowest level Android
has for touch input, filtering right there before anything else even
gets a chance to see the fake touches.

## Features

- Blocks touch bursts above a limit you choose (1-5 fingers)
- Auto-hides a single contact that's been stuck far longer than any real
  touch would be -- no reboot needed
- Live status, finger-count control, and logs, all from a clean in-app
  control panel -- no terminal needed for day-to-day use
- Five themes: Liquid Glass, Material Expressive, Dark, Light, or follow
  your system setting
- No noticeable delay added to normal touches

## Requirements

- A rooted Android device (arm64) with Magisk or a KernelSU-family
  manager (KernelSU, SukiSU, etc.)
- That's it -- no Termux, no compiler, nothing else to install first

## Install

1. Download the latest `TouchGuard-vX.X.zip` from
   [Releases](../../releases)
2. Flash it from your root manager's Modules tab, same as any other
   module
3. Reboot
4. Open TouchGuard from your manager's module list -- that opens the
   built-in control panel

## How to use it

Open the control panel and pick the highest number of fingers you'd
ever use at once -- most people pick 1 or 2. Make sure it shows the
filter running. That's the whole job: no daily maintenance, and
changing your mind later is one tap, no reboot.

Full log is at `/data/adb/modules/touchguard/touchguard.log` if you ever
need it outside the control panel's own Logs tab.

## Turning it off

Instantly, no reboot:
```
su -c "touch /data/local/tmp/touchguard_disable"
```
Or just hit Stop in the control panel.

To stop it from starting at boot entirely:
```
su -c "rm /data/adb/modules/touchguard/enabled"
```
If module doesnt works after reboot 
then go to termux and excute this command by granting root permission 
```
su -c sh /data/adb/modules/touchguard/build.sh
```
and reboot and and check

## If anything ever goes wrong

Reboot and hold **Volume Down** at the boot logo for Magisk Safe Mode --
that disables every module, including this one, no touch input needed.

TouchGuard's grab on the real screen and the virtual one it creates both
live on its own running process. If it crashes or the phone reboots, the
kernel automatically closes both, and normal touch comes back on its
own -- there's no state it can leave your touchscreen in that survives
a reboot.

## How it actually works

It opens the real touchscreen's input device and takes exclusive control
of it (`EVIOCGRAB`) -- the same low-level mechanism that stops Android's
own input system from reading it directly. In its place, it creates a
second, virtual touchscreen device and relays every real touch through
to it -- except frames with more fingers than your chosen limit, which
get dropped, with a clean synthetic "fingers up" sent instead so Android
treats it as a normal release rather than a stuck touch.

## Full version history

See [CHANGELOG.md](CHANGELOG.md) for the complete, warts-and-all
development history -- every bug found, the fixes that didn't actually
work, and why the ones that stuck did.

## Credits

Built by Shivraj ([@Shivraj_editx](https://t.me/Shivraj_editx)) for the
tulip (Redmi Note 6 Pro) modding community.
