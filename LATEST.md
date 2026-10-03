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

