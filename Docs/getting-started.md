# Getting started

A first triggered average, from an empty signal chain. The same five steps set up any of
the four plugins — only what the canvas draws differs.

## 1. Put the plugin in a signal chain

Drag **Triggered Avg** from the processor list into the signal chain, downstream of
whatever produces your continuous data.

![A signal chain with a triggered plugin in it](assets/screenshots/signal-chain.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

Two things about placement:

- **There is no decimation in these plugins.** Cost scales with the sample rate, so if
  you are analysing LFP on a 30 kHz stream, put a downsampling plugin upstream.
- **Filter upstream too.** Nothing here filters; what the plugin sees is what it
  averages.

## 2. Select channels

Use the editor's channel selector. Nothing is selected by default, and with nothing
selected there is nothing to accumulate.

**This is the main cost lever.** Everything downstream — the ring buffer, the capture,
the accumulators, the display — is linear in the number of selected channels. Start with
a handful.

## 3. Set the trial window

`Pre` and `Post`, in milliseconds, are the two boxes on the editor's bottom row. They
default to 500 ms and 1000 ms.

Both are **locked during acquisition**: changing either reallocates the ring buffer and
the accumulators, which discards everything collected so far. That applies to the
channel selection too.

## 4. Define your conditions

Open **TRIGGERS** and add one row per experimental condition. Each row names a TTL line
and, optionally, three broadcast-message patterns.

![TRIGGERS: the trigger source table](assets/screenshots/triggers-window.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

For a first run, one row with a TTL line and no message patterns is enough: it fires on
every rising edge of that line.

The message patterns are what make this useful for a real task — arming a condition on a
trial-type message, and committing or rejecting a trial after the fact on its outcome.
That is the subject of [Triggers and messages](triggers.md), and it is worth reading
before a real experiment.

!!! tip "Type the table once"

    `SAVE` in the TRIGGERS popup writes the whole table — names, TTL lines, colours, the
    three patterns and the timeout — to a small XML file, and `LOAD` reads one back, in
    any of the four plugins. A rig running three of these off the same conditions should
    type the patterns once and load them everywhere else.

## 5. Acquire, and watch MONITOR

Start acquisition. Trials accumulate and the canvas fills in.

If nothing appears, open **MONITOR**. It shows, per condition, the counters for each
stage a trial passes through — TTL edges seen → captures queued → trials captured →
pending committed — plus the total broadcast messages, the text of the last one, and a
one-line diagnosis of the commonest failures.

![MONITOR: per-source counters](assets/screenshots/monitor-window.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

**The stage where the count stops advancing is the stage that is broken:**

| Counters | What it means |
|---|---|
| `EDGES` stays at 0 | Nothing is arriving on that TTL line. Wrong line number, or the events are not reaching this plugin. |
| `EDGES` advances, `QUEUED` does not | The source is gated by an arm pattern and is never being armed. Check the arm pattern against the message text MONITOR shows. |
| `QUEUED` advances, `CAPTURED` does not | The worker gave up on the windows — usually triggers arriving faster than the window is long, or a stream that stopped advancing. |
| `CAPTURED` advances, nothing is kept | The source has a commit pattern and no commit message is matching, or the pending timeout is expiring first. |

Its *Log messages to console* toggle echoes every incoming broadcast message to the GUI
console along with the actions each source took from it. That is how message patterns
get shaped against real message text rather than against what the task is documented to
send.

## 6. Read the canvas

Open the visualizer (the tab or window button on the editor).

![Triggered Average: the canvas](assets/screenshots/average-canvas.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

One panel per selected channel, with each condition drawn in its own colour. The options
bar along the top sets the plot type (average, individual traces, or both), the grid
layout, overlay, and the axis limits. None of those discard data — they are display
controls, applied when the display reads the accumulators.

`CLEAR` discards the accumulated trials and keeps the conditions.

## 7. Save the result

`SAVE` in the canvas's options bar writes a **session** — a directory holding one XML
manifest and one `.npy` per array, plus any exported figures. It works during
acquisition: the node copies its accumulators under their own lock and hands the copy to
a background thread, so saving mid-run costs the capture worker one memcpy.

`LOAD` restores a session, and is disabled while acquiring.

See [Saved sessions](sessions/index.md) for the format, and the
[Python](sessions/python.md) and [MATLAB](sessions/matlab.md) guides for reading one
outside the GUI.

## What to read next

- [Triggers and messages](triggers.md) — the arm/cancel/commit workflow, and why a
  condition fires or does not.
- The page for the plugin you are using: [Triggered Average](plugins/triggered-average.md),
  [Triggered Power](plugins/triggered-power.md),
  [Triggered Coherence](plugins/triggered-coherence.md),
  [Receptive Field Bar Mapper](plugins/receptive-field-mapper.md).
- The [Parameter reference](reference/index.md), when you want the default, the range and
  what changing something costs.
