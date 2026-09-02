# Traces: getting a bug off the wrist

Two features on this fork shipped broken, and both were invisible to every check
we have. CI compiles the firmware but never runs it; `qemu_emery` cannot produce
a held gesture at all, because its touch driver never calls
`touch_handle_gesture()`. So the first time anyone held the watchface, it was on
a wrist — and the report that came back was "the screen started shaking and my
watch restarted", which is a symptom, not a cause.

Prose is a poor bug report, through no fault of whoever writes it. "Swiping
sucks" cannot distinguish a swipe the controller never reported from one that
was reported and rejected, and those have opposite fixes. A trace can.

## Taking one

**Hold the three right-hand buttons — UP, SELECT and DOWN — for five seconds.**
The watch buzzes when the capture is taken.

Not BACK. `SELECT+BACK` held is the hardware reset back door
(`drivers/sf32lb52/debounced_button.c`), so a debug combo including BACK would
reboot the watch instead of capturing the thing you were trying to capture.

The three buttons also do whatever they normally do while you hold them. That is
accepted: suppressing them would mean guessing, at the first press, whether the
other two are coming.

## What is in it

A ring of the last 192 events from the paths that keep going
wrong — raw contacts, the controller's gestures, Stone's own swipe decisions,
what navigation did about them, and the picker and thumbnail cache. It is a ring
on purpose: a capture is always the last few seconds, which are the ones just
before the thing you are trying to explain.

It is deliberately not `PBL_LOG`. A log line costs a format string, a flash write
and a filename — far too much to put on every touch sample — and by the time
anyone looks, the interesting events are drowned by everything else. A
fixed-size record with two integers is cheap enough to leave switched on.

## Reading one

The watch prints it in a terse fixed shape between `STONE-TRACE BEGIN` and
`STONE-TRACE END`. Feed the whole log to the decoder; it finds the capture,
names the codes and shows the gap between events:

```shell
tools/stone/decode_trace.py capture.txt
```

```
     0ms  +0     down                       x=12 y=100
    30ms  +30    move                       x=40 y=104
    46ms  +16    move                       x=78 y=108
    66ms  +20    SWIPE right
```

The gaps are usually the answer. A swipe rejected on duration looks like a long
gap in the middle; a swipe the controller never reported has no `SWIPE` line at
all; a picker showing icons has `thumb absent` lines.

Codes are named in `tools/stone/decode_trace.py` rather than on the watch,
because a name costs flash there and nothing here. An unknown code still
renders, so an older decoder can read a newer capture.

## Getting it to someone who can fix it

The watch has no network of its own — it reaches the world through the phone —
so a capture leaves the same way the firmware log always has, over Bluetooth.

The channel server takes captures and hands them back as plain text:

```shell
curl -X POST "$STONE_CHANNEL_URL/traces?note=swiping+back" \
  -H "Authorization: Bearer $STONE_PUBLISH_TOKEN" \
  --data-binary @capture.txt
```

It answers with a URL. `GET /traces` lists the recent ones and `GET /traces/<id>`
returns one, **both without a token** — the same asymmetry bundles have. Only the
watch's owner can add a capture; anyone helping can read one. A capture is
diagnostic output from our own open-source firmware, and putting a token in front
of reading it would only make it harder to hand to whoever is helping.

Always pass `note=` saying what you were doing. Without it a capture is a wall of
coordinates with no question attached. The note is stored in the capture itself as
well as in the listing, so it survives being copied somewhere else.

The last forty are kept; older ones are deleted with their index entry, so a
listing stays short and nothing is left behind in storage.

**Pasting a capture straight into a conversation works too, and is often
faster.** That is why the format is short and why the decoder tolerates a log
prefix on every line.

## Adding to it

`stone_trace(source, code, a, b)` from anywhere — it does no allocation, no
formatting and no flash, so it is safe in a touch handler. Add the name to
`NAMES` in the decoder in the same commit, or the next person reads a bare
number.

Sources are in `src/fw/debug/stone_trace.h`. Keep codes stable once they are in
a released build: a capture from an older watch should still decode.
