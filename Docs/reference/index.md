# Parameter reference

Every parameter these plugins register, with its default, its range, where it is edited,
and whether changing it discards accumulated data.

| Page | Covers |
|---|---|
| [Shared capture parameters](capture.md) | `channels`, `pre_ms`, `post_ms` and the trigger table — registered by every plugin |
| [Triggered Average](triggered-average.md) | `max_trials` and the six axis-limit parameters |
| [Spectral](spectral.md) | Triggered Power and Triggered Coherence |
| [Receptive Field Bar Mapper](receptive-field.md) | The mapping parameters |

## How to read these tables

**Name** is the parameter's internal name — what appears in a saved signal chain, in a
session's settings block, and in a configuration message sent over the GUI's HTTP
interface. **Label** is what the UI shows.

**Scope** is the Open Ephys parameter scope:

| Scope | Meaning |
|---|---|
| `PROCESSOR` | One value for the whole plugin instance |
| `STREAM` | One value per data stream |

**Locked during acquisition** marks the parameters registered
`deactivateDuringAcquisition`. These are the ones that reshape the accumulators:
changing one reallocates the ring buffer and the per-condition buffers, which **discards
every trial collected so far**.

Everything else stays editable while running, because it only changes how the
accumulated data is drawn.

## The rule behind where a control lives

Stated once in the code, and worth knowing when hunting for a control:

> **Anything that changes what is collected or computed belongs to the editor; anything
> that changes only how the result is drawn belongs to the canvas, beside the plot it
> changes.**

A test asserts that every registered parameter appears in exactly one group of the layout
— which is what stops a parameter being added, wired up, tested, and then left with no
control anywhere.

There are two deliberate departures, both documented where they occur:

- Triggered Power's **baseline** is on the canvas although in Spectrum mode it does
  change what is estimated. It stays there because that is where it is used, and the
  canvas says so rather than hiding it.
- Triggered Coherence's **shift predictor** is under ANALYSIS although it looks like a
  display choice. It accumulates a second estimate as trials arrive and cannot be
  reconstructed afterwards.

## Parameters with no user control

`trigger_line` and `trigger_type` are registered but deliberately not user-facing: they
are the backing store the trigger table writes through. **The table is the UI for these.**

`trigger_type` no longer has anything to choose — `TTL_AND_MSG_TRIGGER` was removed in
0.3.0, and `MSG_TRIGGER` is declared but not implemented. See
[Triggers and messages](../triggers.md).
