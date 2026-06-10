#!/usr/bin/env python3
"""bench_plot.py — Generate performance plots from a BioFMI benchmark CSV.

Usage:
    python3 bench_plot.py [CSV_PATH_OR_RESULTS_DIR]

If no argument is given, the newest *.csv in the script's own results/ directory
is used.  Plots are saved to results/plots/<csv_stem>/ as PNG files.

CSV columns (produced by the 7-cardinal-rules bench suite):
    timestamp, preset, scenario, phase, tool,
    input_size_mb, context_length, pattern_length, n_patterns, n_occurrences, n_reps,
    runtime_median_s, runtime_mean_s, runtime_stddev_s, runtime_p95_s, runtime_p99_s,
    memory_median_mb, memory_mean_mb, memory_stddev_mb, memory_p95_mb, memory_p99_mb

Derived metrics (computed here):
    throughput_mb_s      = input_size_mb / runtime_median_s   (build rows)
    time_per_pattern_ms  = (runtime_median_s * 1000) / n_patterns   (locate rows)
    time_per_occurrence_ms = (runtime_median_s * 1000) / n_occurrences (locate, occ>0)
"""

import re
import sys
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# ---------------------------------------------------------------------------
# Colour scheme — consistent across all plots
# ---------------------------------------------------------------------------

COLORS = {
    "build":  "#2196F3",   # blue  — build phase
    "locate": "#FF9800",   # orange — locate phase
}
ALPHA_BAND = 0.18   # shaded stddev band opacity

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
    df = pd.read_csv(csv_path, dtype={
        "pattern_length":  "Int64",
        "n_patterns":      "Int64",
        "n_occurrences":   "Int64",
        "n_reps":          "Int64",
    })

    # Accept both old (single-value) and new (percentile) column names
    if "runtime_median_s" not in df.columns and "runtime_s" in df.columns:
        df["runtime_median_s"]  = df["runtime_s"]
        df["runtime_stddev_s"]  = 0.0
        df["runtime_p95_s"]     = df["runtime_s"]
        df["memory_median_mb"]  = df["peak_memory_mb"]
        df["memory_stddev_mb"]  = 0.0
        df["memory_p95_mb"]     = df["peak_memory_mb"]

    # Classify
    classified = df["scenario"].apply(classify).apply(pd.Series)
    df = pd.concat([df, classified], axis=1)

    # Derived metrics (using median as the primary value)
    df["throughput_mb_s"] = df.apply(
        lambda r: r["input_size_mb"] / r["runtime_median_s"]
        if r["phase"] == "build" and r["runtime_median_s"] > 0 else float("nan"),
        axis=1,
    )
    df["time_per_pattern_ms"] = df.apply(
        lambda r: (r["runtime_median_s"] * 1000.0) / float(r["n_patterns"])
        if r["phase"] == "locate" and pd.notna(r["n_patterns"]) and float(r["n_patterns"]) > 0
        else float("nan"),
        axis=1,
    )
    df["time_per_occurrence_ms"] = df.apply(
        lambda r: (r["runtime_median_s"] * 1000.0) / float(r["n_occurrences"])
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
# Shared: draw a line + stddev band + optional p95 marker
# ---------------------------------------------------------------------------

def _plot_line_with_band(ax, x, y_med, y_std, *, color, label=None,
                         x_numeric=True, p95=None):
    """Plot median line with ±1 stddev shaded band and optional P95 markers."""
    plot_x = x if x_numeric else list(range(len(x)))

    ax.plot(plot_x, y_med, color=color, marker="o", linewidth=1.8, markersize=6,
            label=label)

    y_lo = [m - s for m, s in zip(y_med, y_std)]
    y_hi = [m + s for m, s in zip(y_med, y_std)]
    ax.fill_between(plot_x, y_lo, y_hi, color=color, alpha=ALPHA_BAND,
                    label="±1 stddev")

    if p95 is not None:
        ax.plot(plot_x, p95, color=color, linestyle=":", linewidth=1.0,
                markersize=4, marker="^", alpha=0.6, label="P95")

    if not x_numeric:
        ax.set_xticks(plot_x)
        ax.set_xticklabels([str(v) for v in x])


# ---------------------------------------------------------------------------
# Build: size sweep  →  build_size_sweep.png
# ---------------------------------------------------------------------------

def plot_build_size_sweep(df: pd.DataFrame, out_dir: Path) -> bool:
    sub = df[df["sweep"] == "build_size"].copy().sort_values("ref_size_mb")
    if sub.empty:
        return False

    fig, (ax_rt, ax_mem) = plt.subplots(1, 2, figsize=(10, 4))

    x      = sub["input_size_mb"].tolist()
    y_rt   = sub["runtime_median_s"].tolist()
    sd_rt  = sub["runtime_stddev_s"].fillna(0).tolist()
    p95_rt = sub["runtime_p95_s"].tolist() if "runtime_p95_s" in sub.columns else None

    y_mem   = sub["memory_median_mb"].tolist()
    sd_mem  = sub["memory_stddev_mb"].fillna(0).tolist()
    p95_mem = sub["memory_p95_mb"].tolist() if "memory_p95_mb" in sub.columns else None

    _plot_line_with_band(ax_rt,  x, y_rt,  sd_rt,  color=COLORS["build"],
                         label="biofmi-build", p95=p95_rt)
    _plot_line_with_band(ax_mem, x, y_mem, sd_mem, color=COLORS["build"],
                         label="biofmi-build", p95=p95_mem)

    ax_rt.set_xlabel("Input l-EDS size (MB)")
    ax_rt.set_ylabel("Runtime (s)  [median ± stddev, P95 dotted]")
    ax_rt.set_title("Build runtime vs EDS size")
    ax_rt.legend(fontsize=8)

    ax_mem.set_xlabel("Input l-EDS size (MB)")
    ax_mem.set_ylabel("Peak memory (MB)  [median ± stddev, P95 dotted]")
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
                xy=(row["input_size_mb"], row["runtime_median_s"]),
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

    rt_med  = [sub.loc[sub["context_l"] == l, "runtime_median_s"].mean() for l in ctx_vals]
    rt_std  = [sub.loc[sub["context_l"] == l, "runtime_stddev_s"].mean() for l in ctx_vals]
    rt_p95  = [sub.loc[sub["context_l"] == l, "runtime_p95_s"].mean()   for l in ctx_vals] \
               if "runtime_p95_s" in sub.columns else None

    mem_med = [sub.loc[sub["context_l"] == l, "memory_median_mb"].mean() for l in ctx_vals]
    mem_std = [sub.loc[sub["context_l"] == l, "memory_stddev_mb"].mean() for l in ctx_vals]
    mem_p95 = [sub.loc[sub["context_l"] == l, "memory_p95_mb"].mean()   for l in ctx_vals] \
               if "memory_p95_mb" in sub.columns else None

    fig, (ax_rt, ax_mem) = plt.subplots(1, 2, figsize=(10, 4))

    _plot_line_with_band(ax_rt,  ctx_vals, rt_med,  rt_std,
                         color=COLORS["build"], x_numeric=False, p95=rt_p95)
    _plot_line_with_band(ax_mem, ctx_vals, mem_med, mem_std,
                         color=COLORS["build"], x_numeric=False, p95=mem_p95)

    ax_rt.set_xlabel("Context length  l")
    ax_rt.set_ylabel("Runtime (s)  [median ± stddev, P95 dotted]")
    ax_rt.set_title("Build runtime vs context length")

    ax_mem.set_xlabel("Context length  l")
    ax_mem.set_ylabel("Peak memory (MB)  [median ± stddev, P95 dotted]")
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

    tpp_med  = [sub.loc[sub["pattern_l"] == p, "time_per_pattern_ms"].mean()     for p in pat_vals]
    tpo_med  = [sub.loc[sub["pattern_l"] == p, "time_per_occurrence_ms"].mean()  for p in pat_vals]
    mem_med  = [sub.loc[sub["pattern_l"] == p, "memory_median_mb"].mean()        for p in pat_vals]

    # Derive stddev for derived metrics proportionally from runtime stddev
    def _rel_std(col_med, col_std, pat):
        rows = sub[sub["pattern_l"] == pat]
        med = rows[col_med].mean()
        std = rows[col_std].mean() if col_std in rows.columns else 0.0
        return (std / med * abs(rows["time_per_pattern_ms"].mean())
                if med and col_med == "runtime_median_s" else 0.0)

    tpp_std  = [sub.loc[sub["pattern_l"] == p, "runtime_stddev_s"].mean() * 1000.0 /
                float(sub.loc[sub["pattern_l"] == p, "n_patterns"].mean() or 1)
                for p in pat_vals]
    tpo_std  = [sub.loc[sub["pattern_l"] == p, "runtime_stddev_s"].mean() * 1000.0 /
                float(sub.loc[sub["pattern_l"] == p, "n_occurrences"].mean() or 1)
                for p in pat_vals]
    mem_std  = [sub.loc[sub["pattern_l"] == p, "memory_stddev_mb"].mean()
                if "memory_stddev_mb" in sub.columns else 0.0
                for p in pat_vals]

    fig, axes = plt.subplots(1, 3, figsize=(14, 4))
    ax_tpp, ax_tpo, ax_mem = axes

    _plot_line_with_band(ax_tpp, pat_vals, tpp_med, tpp_std,
                         color=COLORS["locate"], x_numeric=False)
    _plot_line_with_band(ax_tpo, pat_vals, tpo_med, tpo_std,
                         color=COLORS["locate"], x_numeric=False)
    _plot_line_with_band(ax_mem, pat_vals, mem_med, mem_std,
                         color=COLORS["locate"], x_numeric=False)

    ax_tpp.set_xlabel("Pattern length (bp)")
    ax_tpp.set_ylabel("Time per pattern (ms)  [median ± stddev]")
    ax_tpp.set_title("Search time per pattern")

    ax_tpo.set_xlabel("Pattern length (bp)")
    ax_tpo.set_ylabel("Time per occurrence (ms)  [median ± stddev]")
    ax_tpo.set_title("Search time per occurrence")

    ax_mem.set_xlabel("Pattern length (bp)")
    ax_mem.set_ylabel("Peak memory (MB)  [median ± stddev]")
    ax_mem.set_title("Peak memory")

    # Annotate occurrence counts
    for p in pat_vals:
        row = sub[sub["pattern_l"] == p].iloc[0]
        n_occ = row.get("n_occurrences", 0)
        if pd.notna(n_occ) and n_occ > 0:
            idx = list(pat_vals).index(p)
            ax_tpo.annotate(
                f"occ={int(n_occ)}",
                xy=(idx, tpo_med[idx]),
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

    x       = sub["input_size_mb"].tolist()
    tpp_med = sub["time_per_pattern_ms"].tolist()
    tpp_std = (sub["runtime_stddev_s"] * 1000.0 /
               sub["n_patterns"].astype(float).fillna(1)).tolist()
    mem_med = sub["memory_median_mb"].tolist()
    mem_std = sub["memory_stddev_mb"].fillna(0).tolist() if "memory_stddev_mb" in sub.columns else [0]*len(x)

    fig, (ax_tpp, ax_mem) = plt.subplots(1, 2, figsize=(10, 4))

    _plot_line_with_band(ax_tpp, x, tpp_med, tpp_std, color=COLORS["locate"],
                         label="biofmi-locate")
    _plot_line_with_band(ax_mem, x, mem_med, mem_std, color=COLORS["locate"],
                         label="biofmi-locate")

    ax_tpp.set_xlabel("Index dataset size (MB)")
    ax_tpp.set_ylabel("Time per pattern (ms)  [median ± stddev]")
    ax_tpp.set_title("Locate time/pattern vs dataset size")
    ax_tpp.legend(fontsize=8)

    ax_mem.set_xlabel("Index dataset size (MB)")
    ax_mem.set_ylabel("Peak memory (MB)  [median ± stddev]")
    ax_mem.set_title("Peak memory vs dataset size")
    ax_mem.legend(fontsize=8)

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
# Summary  →  summary.png  (horizontal bars + stddev error bars)
# ---------------------------------------------------------------------------

def _summary_label(scenario: str) -> str:
    replacements = [
        (r"build_size_(\d+)mb",     lambda m: f"build {m.group(1)} MB"),
        (r"build_ctx_l(\d+)",       lambda m: f"build l={m.group(1)}"),
        (r"locate_patlen_l(\d+)",   lambda m: f"locate pat={m.group(1)} bp"),
        (r"locate_dataset_(\d+)mb", lambda m: f"locate {m.group(1)} MB"),
    ]
    for pattern, repl in replacements:
        if m := re.match(pattern, scenario):
            return repl(m)
    return scenario


def plot_summary(df: pd.DataFrame, out_dir: Path) -> bool:
    if df.empty:
        return False

    rows = df.copy()
    rows["label"]     = rows["scenario"].apply(_summary_label)
    rows["bar_color"] = rows["phase"].map(COLORS).fillna("#888888")
    order = {"build": 0, "locate": 1}
    rows["_sort"] = rows["phase"].map(order).fillna(2)
    rows = rows.sort_values(["_sort", "scenario"])

    # Prefer median columns; fall back to old single-value columns
    rt_col  = "runtime_median_s"  if "runtime_median_s"  in rows.columns else "runtime_s"
    mem_col = "memory_median_mb"  if "memory_median_mb"  in rows.columns else "peak_memory_mb"
    rt_err  = rows["runtime_stddev_s"].fillna(0).tolist() if "runtime_stddev_s" in rows.columns else None
    mem_err = rows["memory_stddev_mb"].fillna(0).tolist() if "memory_stddev_mb" in rows.columns else None

    n = len(rows)
    height = max(5.0, n * 0.40)
    fig, (ax_rt, ax_mem) = plt.subplots(1, 2, figsize=(14, height))

    y_pos  = list(range(n))
    labels = rows["label"].tolist()
    colors = rows["bar_color"].tolist()

    ax_rt.barh(y_pos, rows[rt_col].tolist(), color=colors, edgecolor="none",
               xerr=rt_err, error_kw={"ecolor": "#444", "capsize": 3, "linewidth": 0.8})
    ax_rt.set_yticks(y_pos)
    ax_rt.set_yticklabels(labels, fontsize=8)
    ax_rt.set_xlabel("Runtime (s)  [median ± stddev]")
    ax_rt.set_title("Runtime")
    ax_rt.invert_yaxis()

    ax_mem.barh(y_pos, rows[mem_col].tolist(), color=colors, edgecolor="none",
                xerr=mem_err, error_kw={"ecolor": "#444", "capsize": 3, "linewidth": 0.8})
    ax_mem.set_yticks(y_pos)
    ax_mem.set_yticklabels(labels, fontsize=8)
    ax_mem.set_xlabel("Peak memory (MB)  [median ± stddev]")
    ax_mem.set_title("Peak memory")
    ax_mem.invert_yaxis()

    legend_items = [
        mpatches.Patch(color=COLORS["build"],  label="build phase (biofmi-build)"),
        mpatches.Patch(color=COLORS["locate"], label="locate phase (biofmi-locate)"),
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
