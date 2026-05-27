#!/usr/bin/env python3
"""bench_plot.py — Generate performance plots from a BioFMI benchmark CSV.

Usage:
    python3 bench_plot.py [CSV_PATH_OR_RESULTS_DIR]

If no argument is given, the newest *.csv in the script's own results/ directory
is used.  Plots are saved to results/plots/<csv_stem>/ as PNG files.

CSV columns:
    timestamp, preset, scenario, phase, tool,
    input_size_mb, context_length,
    pattern_length, n_patterns, n_occurrences,
    runtime_s, peak_memory_mb

Derived metrics (computed here):
    throughput_mb_s      = input_size_mb / runtime_s          (build rows)
    time_per_pattern_ms  = (runtime_s * 1000) / n_patterns    (locate rows)
    time_per_occurrence_ms = (runtime_s * 1000) / n_occurrences (locate, occ>0)
"""

import re
import sys
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt

# ---------------------------------------------------------------------------
# Colour scheme — consistent across all plots
# ---------------------------------------------------------------------------

COLORS = {
    "build":   "#2196F3",   # blue  — build phase
    "locate":  "#FF9800",   # orange — locate phase
}

# ---------------------------------------------------------------------------
# Scenario classification
# ---------------------------------------------------------------------------

def classify(scenario: str) -> dict:
    """Return structured fields extracted from a scenario name string."""
    if m := re.match(r"build_size_(\d+)mb$", scenario):
        return {"sweep": "build_size", "ref_size_mb": int(m.group(1))}
    if m := re.match(r"build_ctx_l(\d+)$", scenario):
        return {"sweep": "build_ctx", "context_l": int(m.group(1))}
    if m := re.match(r"locate_patlen_l(\d+)$", scenario):
        return {"sweep": "locate_patlen", "pattern_l": int(m.group(1))}
    if m := re.match(r"locate_dataset_(\d+)mb$", scenario):
        return {"sweep": "locate_dataset", "ref_size_mb": int(m.group(1))}
    return {"sweep": "other"}


def load_and_enrich(csv_path: Path) -> pd.DataFrame:
    """Load the CSV, classify scenarios, and compute derived metrics."""
    df = pd.read_csv(csv_path, dtype={"pattern_length": "Int64",
                                       "n_patterns": "Int64",
                                       "n_occurrences": "Int64"})

    # Classify
    classified = df["scenario"].apply(classify).apply(pd.Series)
    df = pd.concat([df, classified], axis=1)

    # Derived metrics
    df["throughput_mb_s"] = df.apply(
        lambda r: r["input_size_mb"] / r["runtime_s"]
        if r["phase"] == "build" and r["runtime_s"] > 0 else float("nan"),
        axis=1,
    )
    df["time_per_pattern_ms"] = df.apply(
        lambda r: (r["runtime_s"] * 1000.0) / float(r["n_patterns"])
        if r["phase"] == "locate" and pd.notna(r["n_patterns"]) and float(r["n_patterns"]) > 0
        else float("nan"),
        axis=1,
    )
    df["time_per_occurrence_ms"] = df.apply(
        lambda r: (r["runtime_s"] * 1000.0) / float(r["n_occurrences"])
        if r["phase"] == "locate" and pd.notna(r["n_occurrences"]) and float(r["n_occurrences"]) > 0
        else float("nan"),
        axis=1,
    )
    return df


# ---------------------------------------------------------------------------
# Matplotlib style
# ---------------------------------------------------------------------------

def _apply_style():
    for style in ("seaborn-v0_8-whitegrid", "seaborn-whitegrid"):
        try:
            plt.style.use(style)
            return
        except OSError:
            pass
    plt.rcParams.update({
        "axes.grid": True,
        "grid.alpha": 0.4,
        "axes.spines.top": False,
        "axes.spines.right": False,
    })


# ---------------------------------------------------------------------------
# Build: size sweep  →  build_size_sweep.png
# ---------------------------------------------------------------------------

def plot_build_size_sweep(df: pd.DataFrame, out_dir: Path) -> bool:
    sub = df[df["sweep"] == "build_size"].copy().sort_values("ref_size_mb")
    if sub.empty:
        return False

    fig, (ax_rt, ax_mem) = plt.subplots(1, 2, figsize=(10, 4))

    x = sub["input_size_mb"].values
    ax_rt.plot(x, sub["runtime_s"].values,
               color=COLORS["build"], marker="o", linewidth=1.8, markersize=6,
               label="biofmi-build")
    ax_mem.plot(x, sub["peak_memory_mb"].values,
                color=COLORS["build"], marker="o", linewidth=1.8, markersize=6,
                label="biofmi-build")

    ax_rt.set_xlabel("Input l-EDS size (MB)")
    ax_rt.set_ylabel("Runtime (s)")
    ax_rt.set_title("Build runtime vs EDS size")
    ax_rt.legend(fontsize=8)

    ax_mem.set_xlabel("Input l-EDS size (MB)")
    ax_mem.set_ylabel("Peak memory (MB)")
    ax_mem.set_title("Build memory vs EDS size")
    ax_mem.legend(fontsize=8)

    _add_throughput_annotation(ax_rt, sub)

    fig.suptitle("biofmi-build: runtime & memory vs input size  (context l=5)",
                 fontsize=11, y=1.02)
    fig.tight_layout()

    out = out_dir / "build_size_sweep.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  build_size_sweep.png")
    return True


def _add_throughput_annotation(ax, sub):
    """Annotate each data point with its throughput (MB/s)."""
    for _, row in sub.iterrows():
        if pd.notna(row.get("throughput_mb_s")) and row["throughput_mb_s"] > 0:
            ax.annotate(
                f"{row['throughput_mb_s']:.1f} MB/s",
                xy=(row["input_size_mb"], row["runtime_s"]),
                xytext=(0, 8), textcoords="offset points",
                fontsize=7, ha="center", color="#555555",
            )


# ---------------------------------------------------------------------------
# Build: context-length sweep  →  build_context_sweep.png
# ---------------------------------------------------------------------------

def plot_build_context_sweep(df: pd.DataFrame, out_dir: Path) -> bool:
    sub = df[df["sweep"] == "build_ctx"].copy().sort_values("context_l")
    if sub.empty:
        return False

    ctx_vals = sorted(sub["context_l"].unique())
    x_labels = [str(int(l)) for l in ctx_vals]

    fig, (ax_rt, ax_mem) = plt.subplots(1, 2, figsize=(10, 4))

    rt_vals  = [sub.loc[sub["context_l"] == l, "runtime_s"].mean()       for l in ctx_vals]
    mem_vals = [sub.loc[sub["context_l"] == l, "peak_memory_mb"].mean()  for l in ctx_vals]

    ax_rt.plot(x_labels, rt_vals,
               color=COLORS["build"], marker="o", linewidth=1.8, markersize=6)
    ax_mem.plot(x_labels, mem_vals,
                color=COLORS["build"], marker="o", linewidth=1.8, markersize=6)

    ax_rt.set_xlabel("Context length  l")
    ax_rt.set_ylabel("Runtime (s)")
    ax_rt.set_title("Build runtime vs context length")

    ax_mem.set_xlabel("Context length  l")
    ax_mem.set_ylabel("Peak memory (MB)")
    ax_mem.set_title("Build memory vs context length")

    input_mb = sub["input_size_mb"].mean()
    fig.suptitle(f"biofmi-build: runtime & memory vs context length  (input ≈{input_mb:.0f} MB)",
                 fontsize=11, y=1.02)
    fig.tight_layout()

    out = out_dir / "build_context_sweep.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  build_context_sweep.png")
    return True


# ---------------------------------------------------------------------------
# Locate: pattern-length sweep  →  locate_pattern_length.png
# ---------------------------------------------------------------------------

def plot_locate_pattern_length(df: pd.DataFrame, out_dir: Path) -> bool:
    sub = df[df["sweep"] == "locate_patlen"].copy().sort_values("pattern_l")
    if sub.empty:
        return False

    pat_vals = sorted(sub["pattern_l"].unique())
    x_labels = [str(int(p)) for p in pat_vals]

    tpp_vals = [sub.loc[sub["pattern_l"] == p, "time_per_pattern_ms"].mean()    for p in pat_vals]
    tpo_vals = [sub.loc[sub["pattern_l"] == p, "time_per_occurrence_ms"].mean() for p in pat_vals]
    mem_vals = [sub.loc[sub["pattern_l"] == p, "peak_memory_mb"].mean()         for p in pat_vals]

    fig, axes = plt.subplots(1, 3, figsize=(14, 4))
    ax_tpp, ax_tpo, ax_mem = axes

    kw = dict(color=COLORS["locate"], marker="o", linewidth=1.8, markersize=6)

    ax_tpp.plot(x_labels, tpp_vals, **kw)
    ax_tpp.set_xlabel("Pattern length (bp)")
    ax_tpp.set_ylabel("Time per pattern (ms)")
    ax_tpp.set_title("Search time per pattern")

    ax_tpo.plot(x_labels, tpo_vals, **kw)
    ax_tpo.set_xlabel("Pattern length (bp)")
    ax_tpo.set_ylabel("Time per occurrence (ms)")
    ax_tpo.set_title("Search time per occurrence")

    ax_mem.plot(x_labels, mem_vals, **kw)
    ax_mem.set_xlabel("Pattern length (bp)")
    ax_mem.set_ylabel("Peak memory (MB)")
    ax_mem.set_title("Peak memory")

    # Annotate occurrence counts
    for p in pat_vals:
        row = sub[sub["pattern_l"] == p].iloc[0]
        n_occ = row.get("n_occurrences", 0)
        if pd.notna(n_occ) and n_occ > 0:
            idx = x_labels.index(str(int(p)))
            ax_tpo.annotate(
                f"occ={int(n_occ)}",
                xy=(idx, tpo_vals[idx]),
                xytext=(0, 8), textcoords="offset points",
                fontsize=7, ha="center", color="#555555",
            )

    input_mb = sub["input_size_mb"].mean()
    fig.suptitle(
        f"biofmi-locate: search time vs pattern length  (index ≈{input_mb:.0f} MB, l=5)",
        fontsize=11, y=1.02,
    )
    fig.tight_layout()

    out = out_dir / "locate_pattern_length.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  locate_pattern_length.png")
    return True


# ---------------------------------------------------------------------------
# Locate: dataset-size sweep  →  locate_dataset_size.png
# ---------------------------------------------------------------------------

def plot_locate_dataset_size(df: pd.DataFrame, out_dir: Path) -> bool:
    sub = df[df["sweep"] == "locate_dataset"].copy().sort_values("ref_size_mb")
    if sub.empty:
        return False

    fig, (ax_tpp, ax_mem) = plt.subplots(1, 2, figsize=(10, 4))

    x = sub["input_size_mb"].values
    kw = dict(color=COLORS["locate"], marker="o", linewidth=1.8, markersize=6)

    ax_tpp.plot(x, sub["time_per_pattern_ms"].values, **kw)
    ax_tpp.set_xlabel("Index dataset size (MB)")
    ax_tpp.set_ylabel("Time per pattern (ms)")
    ax_tpp.set_title("Locate time/pattern vs dataset size")

    ax_mem.plot(x, sub["peak_memory_mb"].values, **kw)
    ax_mem.set_xlabel("Index dataset size (MB)")
    ax_mem.set_ylabel("Peak memory (MB)")
    ax_mem.set_title("Peak memory vs dataset size")

    pat_len = int(sub["pattern_length"].iloc[0]) if "pattern_length" in sub.columns else "?"
    fig.suptitle(
        f"biofmi-locate: performance vs dataset size  (pattern length={pat_len} bp, l=5)",
        fontsize=11, y=1.02,
    )
    fig.tight_layout()

    out = out_dir / "locate_dataset_size.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  locate_dataset_size.png")
    return True


# ---------------------------------------------------------------------------
# Summary  →  summary.png
# ---------------------------------------------------------------------------

def _summary_label(scenario: str) -> str:
    """Shorten a scenario name for the horizontal bar chart Y-axis."""
    replacements = [
        (r"build_size_(\d+)mb",    lambda m: f"build {m.group(1)} MB"),
        (r"build_ctx_l(\d+)",      lambda m: f"build l={m.group(1)}"),
        (r"locate_patlen_l(\d+)",  lambda m: f"locate pat={m.group(1)} bp"),
        (r"locate_dataset_(\d+)mb", lambda m: f"locate {m.group(1)} MB"),
    ]
    for pattern, repl in replacements:
        if m := re.match(pattern, scenario):
            return repl(m)
    return scenario


def _summary_color(phase: str) -> str:
    return COLORS.get(phase, "#888888")


def plot_summary(df: pd.DataFrame, out_dir: Path) -> bool:
    if df.empty:
        return False

    rows = df.copy()
    rows["label"] = rows["scenario"].apply(_summary_label)
    rows["bar_color"] = rows["phase"].apply(_summary_color)
    # Sort: build scenarios first, then locate; within each group by scenario name
    order = {"build": 0, "locate": 1}
    rows["_sort"] = rows["phase"].map(order).fillna(2)
    rows = rows.sort_values(["_sort", "scenario"])

    n = len(rows)
    height = max(5.0, n * 0.40)
    fig, (ax_rt, ax_mem) = plt.subplots(1, 2, figsize=(14, height))

    y_pos = range(n)
    labels = rows["label"].tolist()
    colors = rows["bar_color"].tolist()

    ax_rt.barh(list(y_pos), rows["runtime_s"].tolist(), color=colors, edgecolor="none")
    ax_rt.set_yticks(list(y_pos))
    ax_rt.set_yticklabels(labels, fontsize=8)
    ax_rt.set_xlabel("Runtime (s)")
    ax_rt.set_title("Runtime")
    ax_rt.invert_yaxis()

    ax_mem.barh(list(y_pos), rows["peak_memory_mb"].tolist(), color=colors, edgecolor="none")
    ax_mem.set_yticks(list(y_pos))
    ax_mem.set_yticklabels(labels, fontsize=8)
    ax_mem.set_xlabel("Peak memory (MB)")
    ax_mem.set_title("Peak memory")
    ax_mem.invert_yaxis()

    # Legend
    from matplotlib.patches import Patch
    legend_items = [
        Patch(color=COLORS["build"],  label="build phase (biofmi-build)"),
        Patch(color=COLORS["locate"], label="locate phase (biofmi-locate)"),
    ]
    fig.legend(handles=legend_items, loc="lower center", ncol=2,
               fontsize=9, bbox_to_anchor=(0.5, -0.05))

    ts = rows["timestamp"].iloc[0] if "timestamp" in rows.columns else ""
    fig.suptitle(f"BioFMI benchmark summary  {ts}", fontsize=11)
    fig.tight_layout()

    out = out_dir / "summary.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  summary.png")
    return True


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def parse_args() -> Path:
    if len(sys.argv) > 2:
        print("Usage: bench_plot.py [CSV_PATH_OR_RESULTS_DIR]", file=sys.stderr)
        sys.exit(1)

    results_dir = Path(__file__).parent / "results"

    if len(sys.argv) == 1:
        csvs = sorted(results_dir.glob("*.csv"))
        if not csvs:
            print(f"No CSV files found in {results_dir}", file=sys.stderr)
            sys.exit(1)
        return csvs[-1]

    target = Path(sys.argv[1])
    if target.is_dir():
        csvs = sorted(target.glob("*.csv"))
        if not csvs:
            print(f"No CSV files found in {target}", file=sys.stderr)
            sys.exit(1)
        return csvs[-1]

    if not target.exists():
        print(f"File not found: {target}", file=sys.stderr)
        sys.exit(1)
    return target


def main():
    _apply_style()

    csv_path = parse_args()
    print(f"Reading: {csv_path}")

    df = load_and_enrich(csv_path)

    out_dir = csv_path.parent / "plots" / csv_path.stem
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Plots → {out_dir}/")

    plot_fns = [
        plot_build_size_sweep,
        plot_build_context_sweep,
        plot_locate_pattern_length,
        plot_locate_dataset_size,
        plot_summary,
    ]

    generated = 0
    for fn in plot_fns:
        try:
            ok = fn(df, out_dir)
            if not ok:
                print(f"  (skipped {fn.__name__.replace('plot_', '')}.png — no matching data)")
            else:
                generated += 1
        except Exception as e:
            print(f"  WARNING: {fn.__name__} failed: {e}")

    print(f"\n{generated}/{len(plot_fns)} plots generated.")


if __name__ == "__main__":
    main()
