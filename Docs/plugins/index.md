# The plugins

Four plugins, built from one repository and sharing the same capture layer. Each installs
as its own binary; you can install one without the others.

| Plugin | In the GUI | Binary | Needs FFTW |
|---|---|---|---|
| [Triggered Average](triggered-average.md) | `Triggered Avg` | `TriggeredAverage` | no |
| [Triggered Power](triggered-power.md) | `Triggered Power` | `TriggeredPower` | yes |
| [Triggered Coherence](triggered-coherence.md) | `Triggered Coherence` | `TriggeredCoherence` | yes |
| [Receptive Field Bar Mapper](receptive-field-mapper.md) | `RF Barmapper` | `ReceptiveFieldBarMapper` | no |

The two frequency-domain plugins need the [FFTW runtime](../installation.md#installing-a-release)
in the GUI's `shared` directory; the other two need nothing beyond the plugin binary.

## The editor, in every plugin

All four editors have the same shape:

```
 TRIGGERS   MONITOR   ANALYSIS
 Channels [ ......... ]  ( CH PAIRS — Coherence only )
 Pre [ 500 ] ms   Post [ 1000 ] ms
```

| Control | What it opens |
|---|---|
| **TRIGGERS** | The condition table. See [Triggers and messages](../triggers.md). Carries a badge with the source count. |
| **MONITOR** | Live per-source counters through every stage, and a diagnosis of the commonest failures. |
| **ANALYSIS** | Everything that reshapes the accumulators, so everything here is locked during acquisition. Contents differ per plugin. |
| **Channels** | The channel selection. The main cost lever, and locked during acquisition. |
| **Pre / Post** | The trial window in milliseconds. Locked during acquisition. |

The rule throughout: **anything that changes what is collected or computed belongs to the
editor; anything that changes only how the result is drawn belongs to the canvas.**
