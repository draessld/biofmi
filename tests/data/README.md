# Sample panel

A small committed panel, ~80 KB in total, used by
[`tests/e2e/test_sample_panel.sh`](../e2e/test_sample_panel.sh).

| File | What |
|---|---|
| `sample.eds` | the panel: 1,401 symbols, 700 degenerate, 8 genomes |
| `sample.seds` | its source sets, one entry per string |
| `sample.l5.leds` | the l-EDS at `l = 5`, produced by `eds2leds` |
| `sample.l5.seds` | the merged sources |
| `sample.patterns` | 40 patterns of length 48, each a substring of one genome |

## Why it exists

The other e2e fixtures are hand-written 40-character EDS strings. Those are
right for checking exact positions by hand, and useless for anything structural:
they have no merge behind them, no real source sets, and no pattern long enough
to cross several degenerate symbols.

This panel is sized to exercise what they cannot, while staying small enough to
commit:

- **The merge does real work.** A quarter of the internal common blocks are cut
  below `l`, so `eds2leds` has something to do — 1,401 symbols merge down to
  1,059. A panel that already satisfies the l-EDS property would make the merge
  a no-op, and the fixture could not catch a merger that silently emits l-EDS
  containing strings no genome carries. That has happened.
- **Empty alternatives are common.** 286 of the 2,589 strings are the empty
  string. A zero-length alternative is traversed by a match without contributing
  a character, and it broke the chunk stitch twice.
- **The two search modes actually differ.** At `|P| = 48` — eight full chunks at
  `l = 5`, so each pattern spans several degenerate symbols — CARTESIAN reports
  94 entries and LINEAR 45, with no pattern lost. That is issue B4 reproduced on
  80 KB: the cross product admits combinations no genome carries, and the source
  intersection removes them without touching recall. At `|P| = 12` the two modes
  returned identical counts, which is why the patterns are 48.

## Regenerating

```bash
bash tests/data/regenerate.sh
```

Deterministic — same seed, same bytes. **Do not run it to make a failing test
pass.** These files are the fixture `test_sample_panel.sh` checks against;
regenerating them silently converts a real failure into a green run. Regenerate
only when the panel is meant to change, and look at the diff when you do.

The input comes from `make_sample_eds.py` rather than `genrandomeds`, whose
`--ref-size-mb` is an integer and so cannot produce anything below a 1 Mbp
reference — about a 2 MB `.eds`, and a double-digit-MB l-EDS after the merge.
Everything derived from that input is still made by the real tools, so the
committed artifacts come from the real code path.

## Size discipline

`.gitignore` ignores `*.eds`, `*.leds`, `*.seds`, `*.edz` and `*.patterns`
repo-wide, with a narrow exception for this directory and the e2e fixtures.
Nothing larger belongs in the remote: the benchmark suite generates its panels
into a `mktemp -d` and deletes them, because a 10 MB reference becomes a
~100 MB l-EDS and a ~230 MB index. See
[`docs/benchmarks.md`](../../docs/benchmarks.md).
