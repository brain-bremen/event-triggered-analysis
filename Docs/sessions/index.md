# Saved sessions

A **session** is what these plugins write when you press `SAVE`: a directory holding one
XML manifest, one `.npy` per array, and any exported figures.

It is two things at once:

- **resumable state** — LOAD restores it into the GUI, so a run can be continued later;
- **how the data leaves this program** — everything the display drew is written alongside
  the raw accumulators, so reading a session in Python or MATLAB does not mean
  reimplementing the pipeline.

![A saved session directory](../assets/screenshots/session-directory.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

## Which plugins write them

| Plugin | Sessions |
|---|---|
| [Triggered Average](../plugins/triggered-average.md) | **yes** |
| [Receptive Field Bar Mapper](../plugins/receptive-field-mapper.md) | **yes** |
| [Triggered Power](../plugins/triggered-power.md) | no |
| [Triggered Coherence](../plugins/triggered-coherence.md) | no |

The two spectral plugins have no SAVE / LOAD buttons on their canvases: the shared
machinery is in place, the gather/apply for the spectral accumulators is not.

## Saving

`SAVE` in the canvas's options bar. It suggests a directory name that sorts
chronologically: the plugin, then when it was saved.

**SAVE works during acquisition.** The node copies its accumulators under their own lock
and hands the copy to a background I/O thread, so saving mid-run costs the capture worker
one memcpy.

The bundle is written into a sibling `.<name>.partial` directory and moved into place
only once every file is on disk, so an interrupted save leaves the previous session
intact. `session.xml` is written last.

## Loading

`LOAD` **does not work during acquisition**: restoring accumulators replaces the buffers
the capture worker is writing into. The button disables itself, and the node refuses a
load independently of the button.

A load is one of three outcomes:

| Verdict | When | What happens |
|---|---|---|
| **Resume** | The stored trigger sources match the current ones | The accumulators are restored straight into them |
| **Rebuild** | There are no trigger sources configured yet | The sources are created from the file first, then the accumulators restored. This is the "open the GUI, load yesterday's session, keep mapping" path |
| **Refuse** | Anything else | Nothing is loaded, and the reason is shown |

Two source lists correspond when they have the **same count** and each pair agrees on
**what fires it** — the arm pattern and the TTL line. Names and colours may differ; the
arm pattern may not, because a source matched to the wrong condition produces a plausible
and wrong result.

The trial geometry must match exactly — sample rate, `pre_samples`, `post_samples`,
channel count. Sample rate is compared with a tolerance, so 30000.0 and 29999.9998 are
the same rig.

A refusal names what differs, one line per reason.

!!! note "Sessions are refused across plugins"

    A Triggered Average session will not load into the Bar Mapper, even though the
    accumulator arrays are identical.

!!! note "Demo data is marked"

    A session written from simulated data carries `demo_data="1"` in its manifest.

## Next

- [Format](format.md) — the directory layout, the manifest, and every array.
- [Loading in Python](python.md)
- [Loading in MATLAB](matlab.md)
