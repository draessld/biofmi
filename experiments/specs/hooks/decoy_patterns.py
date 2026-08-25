"""Decoy pattern set: the strings a CARTESIAN representation invents.

Generated ignoring sources — so alternatives are picked independently per
symbol, which samples the cartesian language rather than any genome — then
filtered to those a LINEAR index does *not* contain. What survives is exactly
the set of strings that exist in the cartesian merge and in no real haplotype,
which is what makes the precision difference measurable rather than asserted.

This is a hook rather than a `cmd:` because the filter needs a built index, and
which index is only known after the build stage: the largest l whose LINEAR
build actually succeeded.
"""

from __future__ import annotations

from pathlib import Path


def generate(ctx) -> None:
    pattern_len = ctx.axes.get("pattern_len") or ctx.vars.get("pattern_len")
    n_want = int(ctx.vars.get("n_patterns", 200))
    oversample = int(ctx.vars.get("decoy_oversample", 4))
    seed = int(ctx.vars.get("seed", 0))

    ref = _reference_index(ctx, pattern_len)
    if ref is None:
        ctx.log.warn(
            f"decoy/{ctx.dataset}: no successful linear build to filter against — "
            f"skipping this set"
        )
        return
    ref_cell, ref_l, ref_index = ref
    ctx.log.info(f"decoy/{ctx.dataset}: filtering against {ref_cell} (l={ref_l})")

    ds = ctx.spec.datasets[ctx.dataset]
    work = ctx.out_path.parent
    raw = work / f"{ctx.out_path.stem}.raw.txt"

    res = ctx.run(
        f"{ctx.binaries['edsparser-genpatterns']} -i {ds.inputs['eds']} -o {raw} "
        f"-n {n_want * oversample} -l {pattern_len} --seed {seed} --ignore-sources",
        log_dir=ctx.run_dir / "raw" / "queryset" / f"{ctx.dataset}-decoy",
        log_prefix="genpatterns",
        cap_bytes=ctx.spec.policy.mem_cap,
        timeout_s=ctx.spec.policy.timeout_s,
    )
    if not res.ok or not raw.exists():
        ctx.log.warn(f"decoy/{ctx.dataset}: cartesian pattern generation failed ({res.status})")
        return

    res = ctx.run(
        f"{ctx.binaries['biofmi-locate']} --benchmark -i {ref_index} -l {ref_l} -P {raw}",
        log_dir=ctx.run_dir / "raw" / "queryset" / f"{ctx.dataset}-decoy",
        log_prefix="filter",
        cap_bytes=ctx.spec.policy.mem_cap,
        timeout_s=ctx.spec.policy.timeout_s,
    )
    if not res.ok:
        ctx.log.warn(f"decoy/{ctx.dataset}: filter query failed ({res.status})")
        return

    # `<pattern>\t<count>`; anything with a non-zero count is real, not a decoy.
    found = set()
    for line in res.stdout_text().splitlines():
        pat, _, count = line.partition("\t")
        if count.strip().isdigit() and int(count) > 0:
            found.add(pat)

    kept = []
    for line in raw.read_text().splitlines():
        p = line.strip()
        if p and p not in found:
            kept.append(p)
        if len(kept) >= n_want:
            break

    ctx.out_path.write_text("\n".join(kept) + ("\n" if kept else ""))
    ctx.log.info(
        f"decoy/{ctx.dataset}: {len(kept)} decoys from {oversample * n_want} cartesian "
        f"patterns ({len(found)} were real)"
    )


def _reference_index(ctx, pattern_len) -> tuple[str, int, str] | None:
    """Largest l whose LINEAR build succeeded, with its index path."""
    best = None
    for cell_id, state in ctx.states.items():
        c = state.cell
        if c.dataset != ctx.dataset or c.tool != "linear":
            continue
        if state.status.get("build") != "ok" or "index" not in state.products:
            continue
        if pattern_len is not None and c.params.get("pattern_len") != pattern_len:
            continue
        l = int(c.params["l"])
        if best is None or l > best[1]:
            best = (cell_id, l, state.products["index"])
    return best
