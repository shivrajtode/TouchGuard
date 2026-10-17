## v3.12: a bottom bar that stays put, a log list that stays out of the way, new icons

First round of fixes after using v3.11 on a real device.

The bottom bar moved when you pulled down on any tab. It was a sticky
element inside a page that itself scrolled, so any overscroll dragged it
along with everything else. Fixed at the root rather than patched: the
page no longer scrolls at all. The app is now a fixed-height column with
the bar at the bottom and one inner scroller above it, with page-level
overscroll switched off, so there is nothing left to drag the bar around.
I can't reproduce Android's overscroll stretch in a desktop browser, so
what I verified is that the cause is gone: the document can't scroll,
the bar sits outside the scroller, and raw touch drags (pull-down at the
top, pushing past the bottom) leave it exactly where it was on every
tab. Checked against the previous build, where the same test sees a
scrollable document and a sticky bar. Please confirm on the device.

The log list took over the screen. It could grow to over half the
height and scrolled internally, so most swipes landed on it instead of
the page, and the Tools below were hard to reach. It is now a fixed
height (about a third of the screen, between 200 and 380 px) in both the
plain-language and raw views, with the lines scrolling inside it, so the
page is always reachable around it and the Tools sit right beneath.
Reaching the end of the list no longer drags the page along with it.

Icons redone in the Material 3 Expressive style. Navigation icons come
as an outlined and a filled pair: outlined when idle, filled when
selected, inside a pill indicator that springs open behind the icon,
with a bolder label on the current tab. The event rows, toolbar and
tools use the same rounded family, drawn once in a shared sprite so
each glyph exists exactly once.

Tested with 76 checks on the control flows (the v3.3 and v3.8 guards
among them), 35 on the layout (nav position under scrolling and
touch drags on every tab, the fixed-height panel with 400 lines of log,
reachability of the tools), and 16 on polling, overflow and
accessibility. One of my own first layout checks passed for the wrong
reason, because synthesized touch gestures do nothing in the test
browser; it was redone with raw touch events after a control run proved
those actually scroll. Still not tested inside a real root manager's
WebView.

