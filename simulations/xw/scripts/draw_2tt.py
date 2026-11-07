"""Generate six individual delay plots, the final range/rate figures, and tables.

Run with the repository virtual environment:
    python satnet3d/simulations/xw/scripts/draw_2tt.py

The script reads the three confirmed 2TT result directories and writes PNG,
PDF, SVG, CSV, and XLSX files beneath simulations/xw/images/2tt.
"""
from __future__ import annotations

import os
import re
import sys
import argparse
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.lines import Line2D
from matplotlib.patches import Patch

sys.path.insert(0, os.path.dirname(__file__))
from nc_plot_style import (
    FONT_SIZE_LABEL, FONT_SIZE_TITLE, FONT_SIZE_TICK, FONT_SIZE_LEGEND,
    FONT_SIZE_TEXT, COLOR_NORMAL, COLOR_RETRANSMIT,
    FIG_BG, AX_BG, CARD_EDGE, GRID_COLOR, LEGEND_BG,
    SCHEME_FILE_STEMS,
)

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
OUTPUT = ROOT / "images" / "2tt"
CM_TO_INCH = 1.0 / 2.54
FIG_SIZE_18X15_CM = (18.0 * CM_TO_INCH, 15.0 * CM_TO_INCH)
FONT_SCALE_2TT = FIG_SIZE_18X15_CM[0] / 5.0
FONT_2TT_LABEL = FONT_SIZE_LABEL * FONT_SCALE_2TT
FONT_2TT_TITLE = FONT_SIZE_TITLE * FONT_SCALE_2TT
FONT_2TT_TICK = FONT_SIZE_TICK * FONT_SCALE_2TT
FONT_2TT_LEGEND = FONT_SIZE_LEGEND * FONT_SCALE_2TT
FONT_2TT_TEXT = FONT_SIZE_TEXT * FONT_SCALE_2TT
SCHEMES = [
    ("Best effort", "be-2tt"),
    ("Ground Gated", "jrs-ter-2tt"),
    ("Our Proposal", "jrs-2tt"),
]
SCHEME_COLORS = {"Best effort": "#00A69D", "Ground Gated": "#E64B35", "Our Proposal": "#3C5488"}
FLOW_COLORS = {10: "#3C5488", 11: "#E64B35"}
NORMAL_COLOR = "#3C5488"
RETX_COLOR = "#E64B35"
ALL_FLOWS = range(1, 12)
TS_FLOWS = (10, 11)
SIM_DURATION_S = 61.0
APP_RE = re.compile(r"\.stations\[(\d+)\]\.app\[0\](?:\.(?:source|io))?$")
COUNT_RE = re.compile(r"^scalar\s+(\S+)\s+(packetGenerated|packetSent):count\s+(\d+)\s*$")
PAR_RE = re.compile(r"^par\s+(\S+)\s+(startTime|initialProductionOffset|productionInterval|packetLength)\s+(\S+)\s*$")
UNIT_SCALE = {"s": 1.0, "ms": 1e-3, "us": 1e-6, "ns": 1e-9,
              "B": 8.0, "b": 1.0, "kB": 8e3, "KB": 8e3, "MB": 8e6}


def setup_style():
    mpl.rcParams.update({
        "figure.figsize": FIG_SIZE_18X15_CM,
        "font.family": "sans-serif",
        "font.sans-serif": ["DejaVu Sans", "Arial", "Helvetica"],
        "axes.labelsize": FONT_2TT_LABEL,
        "axes.titlesize": FONT_2TT_TITLE,
        "legend.fontsize": FONT_2TT_LEGEND,
        "xtick.labelsize": FONT_2TT_TICK,
        "ytick.labelsize": FONT_2TT_TICK,
        "axes.labelpad": 2, "axes.titlepad": 4, "axes.linewidth": 1.0,
        "axes.spines.top": True, "axes.spines.right": True,
        "xtick.direction": "in", "ytick.direction": "in",
        "xtick.top": True, "ytick.right": True,
        "xtick.major.size": 3, "ytick.major.size": 3,
        "lines.linewidth": 1.3, "legend.frameon": False,
        "figure.facecolor": FIG_BG, "axes.facecolor": AX_BG,
        "axes.grid": True, "grid.alpha": .25, "grid.linestyle": "--",
        "savefig.dpi": 600,
        "pdf.fonttype": 42, "ps.fonttype": 42, "svg.fonttype": "none",
        "axes.unicode_minus": False,
    })


def add_background(ax, alpha=0.18):
    grad = np.linspace(0.8, 0.0, 256).reshape(-1, 1)
    cmap = LinearSegmentedColormap.from_list("paper_bg", ["#E3DED1", "#EFF3FA"])
    ax.imshow(grad, origin="upper", aspect="auto", cmap=cmap,
              extent=(0, 1, 0, 1), transform=ax.transAxes, alpha=alpha, zorder=0)


def style_axes(ax):
    for spine in ax.spines.values():
        spine.set_color(CARD_EDGE)
        spine.set_linewidth(1.0)
    ax.grid(axis="y", linestyle="--", linewidth=0.55, alpha=0.6, color=GRID_COLOR)


def style_legend(legend):
    if legend is None:
        return
    frame = legend.get_frame()
    frame.set_facecolor(LEGEND_BG)
    frame.set_edgecolor(CARD_EDGE)
    frame.set_linewidth(0.8)
    frame.set_alpha(0.92)


def latest_sca(folder: Path) -> Path:
    files = list(folder.glob("*.sca"))
    if not files:
        raise FileNotFoundError(f"No .sca file in {folder}")
    return max(files, key=lambda p: p.stat().st_mtime)


def scalar_metadata(folder: Path):
    """Return generated count, source start/interval and packet length by flow."""
    counts, pars = {}, {}
    with latest_sca(folder).open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            m = COUNT_RE.match(line)
            if m:
                component, stat, value = m.groups()
                app = APP_RE.search(component)
                if app:
                    flow = int(app.group(1)) + 1
                    counts[(flow, stat)] = int(value)
                continue
            m = PAR_RE.match(line)
            if m:
                component, name, raw = m.groups()
                app = APP_RE.search(component)
                if not app:
                    continue
                flow = int(app.group(1)) + 1
                value_match = re.fullmatch(r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)([A-Za-z]+)?", raw)
                if value_match:
                    value, unit = value_match.groups()
                    pars[(flow, name)] = float(value) * UNIT_SCALE.get(unit or "", 1.0)
    generated = {}
    for flow in ALL_FLOWS:
        generated[flow] = counts.get((flow, "packetGenerated"), counts.get((flow, "packetSent")))
        if generated[flow] is None:
            raise ValueError(f"Missing generated-packet count for Flow {flow} in {latest_sca(folder)}")
    return generated, pars


def read_flow(folder: Path, flow: int) -> pd.DataFrame:
    path = folder / f"flow{flow}.csv"
    if not path.exists():
        return pd.DataFrame()
    return pd.read_csv(path)


def delivered_originals(folder: Path, flow: int) -> pd.DataFrame:
    """One earliest successful reception per original packet, with log-based retry flag."""
    df = read_flow(folder, flow)
    if df.empty:
        return pd.DataFrame(columns=["flow", "sequenceNumber", "sendTime_s", "receiveTime_s", "delay_ms", "retransmitted", "packetBytes"])
    if "attempt" in df:
        df["attempt"] = pd.to_numeric(df.attempt, errors="coerce").fillna(1)
        retry_by_seq = df.groupby("sequenceNumber").attempt.max().gt(1)
    else:
        retry_by_seq = pd.Series(False, index=df.sequenceNumber.drop_duplicates())
    if "outcome" in df:
        df = df[df.outcome.eq("delivered")]
    if df.empty:
        return pd.DataFrame()
    df = df.sort_values("simTime").drop_duplicates("sequenceNumber", keep="first").copy()
    _, params = ACTIVE_META
    start = params.get((flow, "startTime"), params.get((flow, "initialProductionOffset"), 0.0))
    interval = params.get((flow, "productionInterval"))
    if interval is None:
        raise ValueError(f"Missing productionInterval for Flow {flow}; cannot reconstruct original send time")
    df["flow"] = flow
    df["sendTime_s"] = start + pd.to_numeric(df.sequenceNumber) * interval
    df["receiveTime_s"] = pd.to_numeric(df.simTime)
    df["delay_ms"] = pd.to_numeric(df.endToEndDelay) * 1000.0
    df["retransmitted"] = df.sequenceNumber.map(retry_by_seq).fillna(False).astype(bool)
    if "packetBytes" not in df:
        df["packetBytes"] = np.nan
    keep = ["flow", "sequenceNumber", "sendTime_s", "receiveTime_s", "delay_ms", "retransmitted", "packetBytes"]
    return df[keep].sort_values(["sendTime_s", "sequenceNumber"])


def save_figure(fig, stem: str):
    fig.savefig(OUTPUT / f"{stem}.png")
    fig.savefig(OUTPUT / f"{stem}.pdf")
    fig.savefig(OUTPUT / f"{stem}.svg")
    plt.close(fig)


def prepare_data():
    global ACTIVE_META
    all_packets, metadata = {}, {}
    for label, dirname in SCHEMES:
        folder = ROOT / "results" / dirname
        generated, params = scalar_metadata(folder)
        metadata[label] = (generated, params)
        for flow in ALL_FLOWS:
            packets = delivered_originals_with_meta(folder, flow, params)
            all_packets[(label, flow)] = packets
    ACTIVE_META = metadata
    return all_packets, metadata


def delivered_originals_with_meta(folder, flow, params):
    global ACTIVE_META
    ACTIVE_META = (None, params)
    return delivered_originals(folder, flow)


def plot_delay_figures(packets):
    for flow in TS_FLOWS:
        proposed = packets[("Our Proposal", flow)]
        if proposed.empty:
            continue
        x_min = proposed.sequenceNumber.min()
        x_max = proposed.sequenceNumber.max()
        for label, _ in SCHEMES:
            df = packets[(label, flow)]
            if df.empty:
                continue
            fig, ax = plt.subplots(figsize=FIG_SIZE_18X15_CM)
            add_background(ax)
            normal = df[~df.retransmitted]
            retry = df[df.retransmitted]
            # Compare packet counts of the two samples; the bigger one is drawn
            # first so the smaller one is drawn last and stays visible on top.
            series = [(normal, COLOR_NORMAL), (retry, COLOR_RETRANSMIT)]
            series = [item for item in series if not item[0].empty]
            series.sort(key=lambda item: len(item[0]), reverse=True)
            for order, (sample, color) in enumerate(series, start=1):
                _, stems, _ = ax.stem(sample.sequenceNumber, sample.delay_ms,
                                      linefmt=color, markerfmt=" ", basefmt=" ")
                plt.setp(stems, color=color, linewidth=0.25, alpha=0.25, zorder=order)
            ax.set_title(f"Flow {flow} - {label}", fontsize=FONT_2TT_TITLE)
            ax.set_xlabel("Packet index", fontsize=FONT_2TT_LABEL)
            ax.set_ylabel("End-to-end delay (ms)", fontsize=FONT_2TT_LABEL)
            ax.set_xlim(x_min, x_max)
            if label == "Ground Gated":
                ax.set_yscale("log")
            style_axes(ax)
            handles = [
                Line2D([0], [0], color=COLOR_NORMAL, lw=1.2, label="Never retransmitted"),
                Line2D([0], [0], color=COLOR_RETRANSMIT, lw=1.2, label="Retransmitted"),
            ]
            legend = ax.legend(handles=handles, loc="best", fontsize=FONT_2TT_LEGEND,
                               frameon=True, fancybox=True)
            style_legend(legend)
            fig.subplots_adjust(left=0.09, right=0.97, bottom=0.15, top=0.89)
            sname = SCHEME_FILE_STEMS[label]
            save_figure(fig, f"delay_bar_flow{flow}_{sname}_with_retx")


def load_delay_packets_only():
    """Load just the two TS flows without reading large .sca/statistics files."""
    packets = {}
    for label, dirname in SCHEMES:
        folder = ROOT / "results" / dirname
        for flow in TS_FLOWS:
            df = read_flow(folder, flow)
            if df.empty:
                packets[(label, flow)] = df
                continue
            if "attempt" in df:
                attempts = pd.to_numeric(df["attempt"], errors="coerce").fillna(1)
                retransmitted = df.assign(_attempt=attempts).groupby("sequenceNumber")["_attempt"].max().gt(1)
            else:
                retransmitted = pd.Series(False, index=df["sequenceNumber"].drop_duplicates())
            if "outcome" in df:
                df = df[df["outcome"].eq("delivered")]
            df = df.sort_values("simTime").drop_duplicates("sequenceNumber", keep="first").copy()
            df["delay_ms"] = pd.to_numeric(df["endToEndDelay"]) * 1000.0
            df["retransmitted"] = df["sequenceNumber"].map(retransmitted).fillna(False).astype(bool)
            packets[(label, flow)] = df
    return packets


def summarize_samples(packets):
    rows = []
    for label, _ in SCHEMES:
        for flow in TS_FLOWS:
            df = packets[(label, flow)]
            for sample_name, sample in (("All delivered packets", df),
                                        ("Never retransmitted", df[~df.retransmitted])):
                delays = sample.delay_ms.to_numpy(dtype=float)
                rows.append({"scheme": label, "flow": flow, "sample": sample_name,
                             "sample_count": len(delays),
                             "min_delay_ms": delays.min() if len(delays) else np.nan,
                             "max_delay_ms": delays.max() if len(delays) else np.nan,
                             "range_ms": delays.max() - delays.min() if len(delays) else np.nan,
                             "rms_jitter_ms": np.sqrt(np.mean((delays - delays.mean()) ** 2)) if len(delays) else np.nan})
    return pd.DataFrame(rows)


def plot_range(summary):
    fig, ax = plt.subplots(figsize=FIG_SIZE_18X15_CM)
    add_background(ax)
    groups = [(label, flow) for label, _ in SCHEMES for flow in TS_FLOWS]
    x = np.arange(len(groups))
    width = .34
    for offset, sample, color in [(-width/2, "All delivered packets", "#E64B35"),
                                  (width/2, "Never retransmitted", "#3C5488")]:
        values = []
        for label, flow in groups:
            row = summary[(summary.scheme == label) & (summary.flow == flow) & (summary["sample"] == sample)]
            values.append(float(row.range_ms.iloc[0]) if not row.empty else np.nan)
        legend_label = "Initial transmissions" if sample == "Never retransmitted" else sample
        bars = ax.bar(x + offset, values, width, color=color,
                      edgecolor="#333333", linewidth=.5, label=legend_label)
        for bar, value in zip(bars, values):
            if np.isfinite(value):
                ax.annotate(f"{value:.4g}", (bar.get_x()+bar.get_width()/2, value),
                            xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=FONT_2TT_TEXT, rotation=45)
    positive = summary.loc[summary.range_ms > 0, "range_ms"]
    use_log = not positive.empty and positive.max() / positive.min() >= 100
    if use_log:
        ax.set_yscale("log")
        ax.set_ylabel("End-to-end jitter (ms, logarithmic scale)")
        # Leave room for rotated value labels above the tallest log-scale bars.
        # Reserve a blank band at the top of the axes for the in-grid legend.
        ax.set_ylim(max(positive.min() * 0.55, positive.min() / 2.0), positive.max() * 20.0)
    else:
        ax.set_ylabel("End-to-end jitter (ms)")
        ax.set_ylim(0, (positive.max() if not positive.empty else 1.0) * 1.4)
    labels = [f"{label}\nFlow {flow}" for label, flow in groups]
    ax.set_xticks(x, labels, rotation=24, ha="right", rotation_mode="anchor")
    ax.tick_params(axis="x", labelsize=FONT_2TT_TICK)
    ax.set_title("Jitter", fontsize=FONT_2TT_TITLE)
    ax.set_xlabel("Scheme and TS flow", fontsize=FONT_2TT_LABEL, labelpad=-6)
    legend = ax.legend(fontsize=FONT_2TT_LEGEND * 0.85, loc="upper center", ncol=2)
    style_legend(legend)
    style_axes(ax)
    # Match the plotting box and title/axis-label placement used by the delay plots.
    fig.subplots_adjust(left=0.09, right=0.97, bottom=0.15, top=0.89)
    save_figure(fig, "figure_4_jitter")


def plot_delivery(packets, metadata):
    fig, ax = plt.subplots(figsize=FIG_SIZE_18X15_CM)
    add_background(ax)
    x = np.arange(1, 12)
    width = .24
    rows = []
    for si, (label, _) in enumerate(SCHEMES):
        generated, _ = metadata[label]
        received = []
        for flow in ALL_FLOWS:
            count = packets[(label, flow)].sequenceNumber.nunique()
            received.append(count)
            rows.append({"scheme": label, "flow": flow, "generated_original_packets": generated[flow],
                         "successfully_received_unique_original_packets": count,
                         "success_rate_percent": 100.0 * count / generated[flow] if generated[flow] else np.nan,
                         "window_s": SIM_DURATION_S, "in_flight_at_end": "included if received before simulation end"})
        rates = np.array([100.0 * received[i] / generated[i+1] if generated[i+1] else 0 for i in range(11)])
        ax.bar(x + (si-1)*width, rates, width, color=SCHEME_COLORS[label], label=label)
    ax.axvspan(9.5, 11.5, color="#F2C14E", alpha=.13, zorder=0)
    ax.text(10.5, 101, "TS flows", ha="center", va="bottom", fontsize=FONT_2TT_TEXT)
    ax.set_xlim(.5, 11.5)
    # Keep the displayed scale at 0–100%; reserve a small unlabeled band inside
    # the axes above 100% for the legend.
    ax.set_ylim(0, 112)
    ax.set_yticks(np.arange(0, 101, 20))
    ax.set_xticks(x, [str(i) for i in x])
    ax.tick_params(axis="x", labelsize=FONT_2TT_TICK)
    ax.set_ylabel("Successful reception rate (%)", fontsize=FONT_2TT_LABEL)
    ax.set_xlabel("Flow (1–11)", fontsize=FONT_2TT_LABEL)
    ax.set_title("Success rate", fontsize=FONT_2TT_TITLE)
    legend = ax.legend(ncol=3, fontsize=FONT_2TT_LEGEND * 0.9, loc="upper center")
    style_legend(legend)
    style_axes(ax)
    # Keep the axes box and label/title positions consistent with the delay plots.
    fig.subplots_adjust(left=0.09, right=0.97, bottom=0.15, top=0.89)
    save_figure(fig, "figure_5_success_rate")
    return pd.DataFrame(rows)


def make_tables(packets, metadata):
    summary = summarize_samples(packets)
    ranges = summary[["scheme", "flow", "sample", "sample_count", "min_delay_ms", "max_delay_ms", "range_ms"]].copy()
    rates = plot_delivery(packets, metadata)
    rms_rows = []
    for label, _ in SCHEMES:
        for flow in TS_FLOWS:
            full = summary[(summary.scheme == label) & (summary.flow == flow) & (summary["sample"] == "All delivered packets")].iloc[0]
            filtered = summary[(summary.scheme == label) & (summary.flow == flow) & (summary["sample"] == "Never retransmitted")].iloc[0]
            rms_rows.append({"scheme": label, "flow": flow, "all_sample_count": full.sample_count,
                             "all_sample_rms_ms": full.rms_jitter_ms,
                             "filtered_sample_count": filtered.sample_count,
                             "filtered_sample_rms_ms": filtered.rms_jitter_ms})
    rms = pd.DataFrame(rms_rows)

    tp_rows = []
    for label, _ in SCHEMES:
        generated, params = metadata[label]
        ideal_bits = actual_bits = 0.0
        for flow in ALL_FLOWS:
            packet_length = params.get((flow, "packetLength"))
            if packet_length is None:
                # Infer bits from the recorded packetBytes for non-source metadata variants.
                sample = read_flow(ROOT / "results" / dict(SCHEMES)[label], flow)
                if sample.empty or "packetBytes" not in sample:
                    raise ValueError(f"Missing packetLength and packetBytes for Flow {flow} in {label}")
                packet_length = float(sample.packetBytes.iloc[0]) * 8
            ideal_bits += generated[flow] * packet_length
            sample = packets[(label, flow)]
            if not sample.empty:
                packet_bytes = pd.to_numeric(sample.packetBytes, errors="coerce")
                actual_bits += float(packet_bytes.fillna(0).sum()) * 8
                # Reliable-source CSV exports omit packetBytes; use that
                # source's configured packetLength, already normalized to bits.
                actual_bits += int(packet_bytes.isna().sum()) * packet_length
        ideal_gbps = ideal_bits / SIM_DURATION_S / 1e9
        actual_gbps = actual_bits / SIM_DURATION_S / 1e9
        tp_rows.append({"scheme": label, "actual_throughput_Gbps": actual_gbps,
                        "ideal_throughput_Gbps": ideal_gbps,
                        "actual_to_ideal_percent": 100 * actual_gbps / ideal_gbps if ideal_gbps else np.nan})
    throughput = pd.DataFrame(tp_rows)
    return ranges, rms, rates, throughput


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    setup_style()
    parser = argparse.ArgumentParser()
    parser.add_argument("--jitter-only", action="store_true",
                        help="regenerate only the jitter figure from its summary CSV")
    parser.add_argument("--delay-only", action="store_true",
                        help="regenerate only the six Flow 10/11 delay figures")
    args = parser.parse_args()
    if args.jitter_only:
        summary = pd.read_csv(OUTPUT / "delay_range_source.csv")
        plot_range(summary)
        print(f"Updated jitter figure in {OUTPUT}")
        return
    if args.delay_only:
        plot_delay_figures(load_delay_packets_only())
        print(f"Updated six delay figures in {OUTPUT}")
        return
    packets, metadata = prepare_data()
    source = pd.concat([df.assign(scheme=label) for (label, _flow), df in packets.items() if not df.empty], ignore_index=True)
    source.to_csv(OUTPUT / "packet_source_data.csv", index=False)
    plot_delay_figures(packets)
    ranges, rms, rates, throughput = make_tables(packets, metadata)
    plot_range(summarize_samples(packets))
    for name, table in (("table_1_throughput", throughput), ("table_2_rms", rms)):
        table.to_csv(OUTPUT / f"{name}.csv", index=False)
    rates.to_csv(OUTPUT / "flow_success_rate_source.csv", index=False)
    ranges.to_csv(OUTPUT / "delay_range_source.csv", index=False)
    with pd.ExcelWriter(OUTPUT / "tables_editable.xlsx") as writer:
        throughput.to_excel(writer, sheet_name="Table 1 Throughput", index=False)
        rms.to_excel(writer, sheet_name="Table 2 RMS", index=False)
        rates.to_excel(writer, sheet_name="Flow success rate data", index=False)
        ranges.to_excel(writer, sheet_name="Delay range data", index=False)
    (OUTPUT / "methodology.txt").write_text(
        "All outputs use the three result folders configured in draw_2tt_final.py.\n"
        "Simulation window: 0 to 61 s, read from the 2TT simulation configuration.\n"
        "For Flow 10/11, send time = startTime + sequenceNumber * productionInterval, using recorded .sca parameters; the plotted delay is endToEndDelay.\n"
        "Delivered packets are unique sequenceNumber values with a delivered outcome; first successful receive time is the minimum simTime. They include originals that were retransmitted.\n"
        "A packet is classified as retransmitted if any logged attempt for that sequenceNumber exceeds 1.\n"
        "Filtered samples remove every original packet ever retransmitted.\n"
        "Flow success denominators are packetGenerated counts (or packetSent for sources without packetGenerated); repeated copies are counted once on reception. In-flight packets at simulation end remain in the generated denominator and are not treated as received. This is success rate, not deadline-compliant delivery rate.\n"
        "Throughput uses total packet bits over 61 s, across Flows 1-11. Ideal bits = generated original packets * configured packetLength. Actual bits = packetBytes of uniquely delivered originals; retransmission copies are not counted again.\n"
        "RMS is the population standard deviation of delays in each stated sample, in ms.\n",
        encoding="utf-8")
    print(f"Wrote final 2TT figures, tables, and source data to {OUTPUT}")


if __name__ == "__main__":
    main()
