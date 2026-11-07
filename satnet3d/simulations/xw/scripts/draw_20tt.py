"""20-TS-flow scenario plotting script (pandas version).
Reads CSV data from ../results/{jrs-20tt,jrs-ter-20tt,be-20tt}/ and outputs 8 figures:
  3 box plots (delay distribution for flows 10-29 per scheme)
  3 flow arrival rate plots (per-flow packet delivery ratio)
  2 delay-range box plots comparing per-flow delay range, with one variant of
    each excluding retransmission-delivered packets

Canvas size, font scaling and plot margins follow draw_2tt.py so every figure
in images/2tt and images/20tt shares the same physical layout.
"""
from __future__ import annotations
import os, sys, math, re, glob
import numpy as np
import pandas as pd
import matplotlib as mpl
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
from matplotlib.colors import LinearSegmentedColormap

sys.path.insert(0, os.path.dirname(__file__))
from nc_plot_style import (
    FONT_FAMILY, FONT_SIZE_LABEL, FONT_SIZE_TITLE, FONT_SIZE_TICK,
    FONT_SIZE_LEGEND, FONT_SIZE_TEXT, COLOR_SCHEME,
    COLOR_ACTUAL, COLOR_THEORY,
    FIG_BG, AX_BG, CARD_EDGE, GRID_COLOR, LEGEND_BG,
    SCHEME_FILE_STEMS,
)

CM_TO_INCH = 1.0 / 2.54
FIG_HEIGHT_CM = 15.0
# 18 x 15 cm canvas for the delay-range figures, identical to the 2TT figures.
FIG_SIZE = (18.0 * CM_TO_INCH, FIG_HEIGHT_CM * CM_TO_INCH)
# The delay-distribution and arrival-rate figures carry two-digit x tick labels
# (1..20 and 1..29). 18 cm is too narrow to keep them upright, so they use a
# wider canvas of the same height; 21 cm gives every label a full slot:
#   slot = 0.82 * 21 cm / 29 = 0.59 cm  >  two-digit label 0.51 cm
FIG_SIZE_WIDE = (21.0 * CM_TO_INCH, FIG_HEIGHT_CM * CM_TO_INCH)
# The 20TT fonts were sized for a 5-inch-wide canvas; scale them by the same
# factor draw_2tt.py uses so the rendered font sizes match exactly.
FONT_SCALE = FIG_SIZE[0] / 5.0
FONT_LABEL = FONT_SIZE_LABEL * FONT_SCALE
FONT_TITLE = FONT_SIZE_TITLE * FONT_SCALE
FONT_TICK = FONT_SIZE_TICK * FONT_SCALE
FONT_LEGEND = FONT_SIZE_LEGEND * FONT_SCALE
FONT_TEXT = FONT_SIZE_TEXT * FONT_SCALE
# Shared plot-box margins, matching draw_2tt.py.
MARGINS = dict(left=0.09, right=0.97, bottom=0.15, top=0.89)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_ROOT = os.path.join(SCRIPT_DIR, '..', 'results')
OUTPUT_DIR = os.path.join(SCRIPT_DIR, '..', 'images', '20tt')
os.makedirs(OUTPUT_DIR, exist_ok=True)

SCHEME_DIRS = [
    os.path.join(RESULTS_ROOT, 'be-20tt'),
    os.path.join(RESULTS_ROOT, 'jrs-ter-20tt'),
    os.path.join(RESULTS_ROOT, 'jrs-20tt'),
]
SCHEME_LABELS = ['Best effort', 'Ground Gated', 'Our Proposal']
SCHEME_COLORS = [COLOR_SCHEME[1], COLOR_SCHEME[0], COLOR_SCHEME[2]]

TS_FLOWS = list(range(10, 30))
ALL_FLOWS = list(range(1, 30))
TS_GENERATION_CUTOFF_S = 59.9
TS_GENERATION_CUTOFF_EPSILON_S = 1e-6

SOURCE_COUNT_SCALAR_RE = re.compile(
    r'^scalar\s+\S+\.stations\[(\d+)\]\.app\[0\](?:\.io)?\s+'
    r'(packetSent|packetGenerated):count\s+(\d+)\s*$'
)
SOURCE_TIME_PARAMETER_RE = re.compile(
    r'^par\s+\S+\.stations\[(\d+)\]\.app\[0\](?:\.source)?\s+'
    r'(startTime|initialProductionOffset|productionInterval)\s+'
    r'([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)(ns|us|ms|s)\s*$'
)
TIME_UNIT_TO_SECONDS = {'ns': 1e-9, 'us': 1e-6, 'ms': 1e-3, 's': 1.0}


def set_style():
    mpl.rcParams.update({
        'figure.figsize': FIG_SIZE,
        'font.family': 'sans-serif',
        'font.sans-serif': ['DejaVu Sans', 'Arial', 'Helvetica'],
        'axes.labelsize': FONT_LABEL,
        'axes.titlesize': FONT_TITLE,
        'legend.fontsize': FONT_LEGEND,
        'xtick.labelsize': FONT_TICK,
        'ytick.labelsize': FONT_TICK,
        'axes.labelpad': 2, 'axes.titlepad': 4, 'axes.linewidth': 1.0,
        'axes.spines.top': True, 'axes.spines.right': True,
        'xtick.direction': 'in', 'ytick.direction': 'in',
        'xtick.top': True, 'ytick.right': True,
        'xtick.major.size': 3, 'ytick.major.size': 3,
        'lines.linewidth': 1.3, 'legend.frameon': False,
        'figure.facecolor': FIG_BG, 'axes.facecolor': AX_BG,
        'axes.grid': True, 'grid.alpha': .25, 'grid.linestyle': '--',
        'savefig.dpi': 600, 'pdf.fonttype': 42, 'ps.fonttype': 42,
        'svg.fonttype': 'none',
        'axes.unicode_minus': False,
    })


def save_figure(fig, stem):
    """Write PNG, PDF and SVG, matching the 2TT output set."""
    for extension in ('png', 'pdf', 'svg'):
        fig.savefig(os.path.join(OUTPUT_DIR, f'{stem}.{extension}'),
                    dpi=600)
    plt.close(fig)


def add_bg(ax, alpha=0.18):
    grad = np.linspace(0.8, 0.0, 256).reshape(-1, 1)
    cmap = LinearSegmentedColormap.from_list('bg', ['#E3DED1', '#EFF3FA'])
    ax.imshow(grad, origin='upper', aspect='auto', cmap=cmap,
              extent=(0, 1, 0, 1), transform=ax.transAxes, alpha=alpha, zorder=0)


def style_ax(ax):
    for s in ax.spines.values():
        s.set_color(CARD_EDGE); s.set_linewidth(1.0)
    ax.grid(axis='y', linestyle='--', linewidth=0.55, alpha=0.6, color=GRID_COLOR)


def style_leg(leg):
    if leg is None: return
    f = leg.get_frame()
    f.set_facecolor(LEGEND_BG); f.set_edgecolor(CARD_EDGE)
    f.set_linewidth(0.8); f.set_alpha(0.92)


def tick_label_formatter(decimals=None):
    """Plain-decimal ticks below 1000, e-notation (1e3, 1.2e4) at or above it.

    Any value of 1000 or more prints as a run of at least three trailing
    zeros, so those ticks switch to scientific notation.
    """
    def fmt(value, _position=None):
        if not np.isfinite(value):
            return ''
        if abs(value) >= 1000:
            exponent = int(math.floor(math.log10(abs(value))))
            mantissa = f'{value / 10.0 ** exponent:.1f}'.rstrip('0').rstrip('.')
            return f'{mantissa}e{exponent}'
        if decimals is not None:
            return f'{value:.{decimals}f}'
        return f'{value:,.0f}' if float(value).is_integer() else f'{value:g}'
    return mpl.ticker.FuncFormatter(fmt)


def read_ideal_packet_counts(scheme_dir, flow_ids, generation_cutoff_s=None):
    """Read each sender's generated unique-packet count.

    Reliable sources expose packetGenerated, while ordinary EthernetSourceApp
    sources expose io.packetSent. Since ordinary sources do not retransmit,
    their packetSent count is also their generated unique-packet count. When a
    cutoff is supplied, TS-flow counts include packets generated at or before
    that time and exclude later packets.
    """
    scalar_paths = [
        os.path.join(scheme_dir, name)
        for name in os.listdir(scheme_dir)
        if name.endswith('.sca')
    ]
    if not scalar_paths:
        raise FileNotFoundError(f'No .sca result file found in {scheme_dir}')
    scalar_path = max(scalar_paths, key=os.path.getmtime)

    counts_by_station_and_statistic = {}
    source_times = {}
    with open(scalar_path, encoding='utf-8', errors='replace') as scalar_file:
        for line in scalar_file:
            match = SOURCE_COUNT_SCALAR_RE.match(line)
            if match:
                key = (int(match.group(1)), match.group(2))
                counts_by_station_and_statistic[key] = int(match.group(3))
                continue
            match = SOURCE_TIME_PARAMETER_RE.match(line)
            if match:
                station = int(match.group(1))
                parameter = match.group(2)
                value = float(match.group(3)) * TIME_UNIT_TO_SECONDS[match.group(4)]
                source_times[(station, parameter)] = value

    keys = []
    missing = []
    for flow_id in flow_ids:
        station = flow_id - 1
        generated_key = (station, 'packetGenerated')
        sent_key = (station, 'packetSent')
        if generated_key in counts_by_station_and_statistic:
            keys.append(generated_key)
        elif sent_key in counts_by_station_and_statistic:
            keys.append(sent_key)
        else:
            missing.append(flow_id)
    if missing:
        raise ValueError(
            f'Missing sender count statistics for flows {missing} in {scalar_path}; '
            'rerun the simulation with packetGenerated:count or io.packetSent:count enabled'
        )
    counts = np.array(
        [counts_by_station_and_statistic[key] for key in keys], dtype=np.int64
    )
    if generation_cutoff_s is not None:
        for index, flow_id in enumerate(flow_ids):
            if flow_id not in TS_FLOWS:
                continue
            station = flow_id - 1
            interval = source_times.get((station, 'productionInterval'))
            start = source_times.get(
                (station, 'startTime'),
                source_times.get((station, 'initialProductionOffset')),
            )
            if interval is None or start is None or interval <= 0:
                raise ValueError(
                    f'Missing valid source timing for flow{flow_id} in {scalar_path}'
                )
            if generation_cutoff_s + TS_GENERATION_CUTOFF_EPSILON_S < start:
                cutoff_count = 0
            else:
                elapsed = generation_cutoff_s - start
                cutoff_count = math.floor(
                    elapsed / interval + TS_GENERATION_CUTOFF_EPSILON_S
                ) + 1
            counts[index] = min(counts[index], max(cutoff_count, 0))
    return counts


# ── box plots ────────────────────────────────────────────────────────────
def plot_boxplots():
    for s_idx, sdir in enumerate(SCHEME_DIRS):
        fig, ax = plt.subplots(figsize=FIG_SIZE_WIDE)
        add_bg(ax)
        all_delays = []
        for fid in TS_FLOWS:
            path = os.path.join(sdir, f'flow{fid}.csv')
            if os.path.exists(path):
                df = pd.read_csv(path)
                if 'outcome' in df.columns:
                    df = df[df['outcome'] == 'delivered']
                delays = (df['endToEndDelay'] * 1000.0).tolist()
            else:
                delays = []
            all_delays.append(delays)

        labels = [str(i) for i in range(1, 21)]
        bp = ax.boxplot(all_delays, tick_labels=labels, patch_artist=True,
                        showfliers=True,
                        flierprops=dict(marker='o', markersize=2,
                                        markerfacecolor=COLOR_SCHEME[1],
                                        alpha=0.4, markeredgecolor='none'))
        for patch in bp['boxes']:
            patch.set_facecolor(SCHEME_COLORS[s_idx])
            patch.set_alpha(0.7); patch.set_edgecolor(CARD_EDGE); patch.set_linewidth(0.8)
        for element in ['whiskers', 'caps', 'medians']:
            for item in bp[element]:
                item.set_color(CARD_EDGE); item.set_linewidth(0.8)
        for median in bp['medians']:
            median.set_color('white'); median.set_linewidth(1.2)

        ax.set_xlabel('TS flow index', fontsize=FONT_LABEL)
        ax.set_ylabel('End-to-end delay (ms)', fontsize=FONT_LABEL)
        ax.set_title(f'{SCHEME_LABELS[s_idx]} – delay distribution (20 TS flows)',
                     fontsize=FONT_TITLE)
        if SCHEME_LABELS[s_idx] == 'Ground Gated':
            delay_formatter = mpl.ticker.ScalarFormatter(useMathText=True)
            delay_formatter.set_scientific(True)
            delay_formatter.set_powerlimits((0, 0))
            delay_formatter.set_useOffset(False)
            ax.yaxis.set_major_formatter(delay_formatter)
        # Keep the Best Effort flow distributions visible; the extreme
        # TS-flow-18 outliers otherwise stretch the axis to several seconds.
        # if s_idx == 0:
        #     ax.set_ylim(0, 1000)
        style_ax(ax)
        fig.subplots_adjust(**MARGINS)
        sname = SCHEME_FILE_STEMS[SCHEME_LABELS[s_idx]]
        save_figure(fig, f'boxplot_delay_{sname}_20tt')


# ── TS-flow jitter comparison (maximum minus minimum delay) ──────────────

def calculate_flow_delay_range(scheme_dir, flow_id, include_retransmitted=True):
    """Return max(delay) - min(delay), in milliseconds."""
    path = os.path.join(scheme_dir, f'flow{flow_id}.csv')
    if not os.path.exists(path):
        return np.nan
    df = pd.read_csv(path)
    if 'outcome' in df.columns:
        df = df[df['outcome'] == 'delivered']
    if not include_retransmitted and 'attempt' in df.columns:
        df = df[df['attempt'] == 1]
    if len(df) < 2:
        return np.nan
    delays_ms = df['endToEndDelay'].to_numpy(dtype=float) * 1000.0
    return float(delays_ms.max() - delays_ms.min())


def plot_delay_range_boxplot(include_retransmitted=True):
    # Each scheme contributes 20 samples: one delay-range value for each of
    # flow10..flow29, using the same definition as draw_2tt.py.
    metric_ylabel = 'Jitter (ms)'
    filename_stem = ('figure_4_jitter_20tt' if include_retransmitted
                     else 'figure_5_jitter_20tt')

    jitter_by_scheme = []
    for scheme_dir in SCHEME_DIRS:
        values = np.array([
            calculate_flow_delay_range(
                scheme_dir, flow_id,
                include_retransmitted=include_retransmitted,
            )
            for flow_id in TS_FLOWS
        ], dtype=float)
        jitter_by_scheme.append(values[np.isfinite(values)])

    # Ground Gated is several orders of magnitude larger than BE and Proposed.
    # Use two portions of the same y-axis instead of two independently scaled
    # left/right axes. Both portions contain the same three box plots, clipped
    # to their respective low and high ranges.
    fig = plt.figure(figsize=FIG_SIZE)
    grid = fig.add_gridspec(2, 1, height_ratios=[1.0, 1.0], hspace=0.07)
    ax_high = fig.add_subplot(grid[0])
    ax_low = fig.add_subplot(grid[1], sharex=ax_high)
    add_bg(ax_high)
    add_bg(ax_low)

    def style_boxplot(boxplot, colors=SCHEME_COLORS):
        for index, box in enumerate(boxplot['boxes']):
            box.set_facecolor(colors[index])
            box.set_edgecolor(CARD_EDGE)
            box.set_linewidth(0.9)
            box.set_alpha(0.82)
            boxplot['medians'][index].set_color('white')
            boxplot['medians'][index].set_linewidth(0.8)
            if index < len(boxplot['fliers']):
                boxplot['fliers'][index].set_markerfacecolor(colors[index])
        for item in boxplot['whiskers'] + boxplot['caps']:
            item.set_color(CARD_EDGE)
            item.set_linewidth(0.9)

    for axis in (ax_high, ax_low):
        boxplot = axis.boxplot(
            jitter_by_scheme,
            positions=[1, 2, 3],
            widths=0.55,
            patch_artist=True,
            showfliers=True,
            manage_ticks=False,
            flierprops=dict(marker='o', markersize=3, alpha=0.55,
                            markeredgecolor='none'),
        )
        style_boxplot(boxplot)
        axis.set_yscale('linear')
        axis.set_xlim(0.5, 3.5)
        style_ax(axis)

    low_values = np.concatenate([jitter_by_scheme[0], jitter_by_scheme[2]])
    ground_values = jitter_by_scheme[1]
    low_max = max(float(low_values.max()) * 1.25, 1e-9)
    ground_q1 = float(np.percentile(ground_values, 25))
    high_min = ground_q1 * 0.75
    high_max = float(ground_values.max()) * 1.10
    if high_min <= low_max:
        high_min = low_max * 1.5

    ax_low.set_ylim(0, low_max)
    ax_high.set_ylim(high_min, high_max)
    ax_low.yaxis.set_major_locator(mpl.ticker.MaxNLocator(nbins=5))
    ax_high.yaxis.set_major_locator(mpl.ticker.MaxNLocator(nbins=4))
    ax_low.yaxis.set_major_formatter(tick_label_formatter())
    ax_high.yaxis.set_major_formatter(tick_label_formatter())

    # The Proposed distribution is much tighter than the shared low-range
    # scale. Keep the main box geometrically truthful and add an explicitly
    # labelled inset in the otherwise empty upper-right region to reveal its
    # quartiles and whiskers.
    proposed_values = np.sort(jitter_by_scheme[2])
    proposed_min = float(proposed_values.min())
    proposed_max = float(proposed_values.max())
    proposed_span = proposed_max - proposed_min
    proposed_padding = (proposed_span * 0.25 if proposed_span > 0
                        else max(abs(proposed_min) * 0.02, 1e-6))
    proposed_inset = ax_high.inset_axes([0.72, 0.13, 0.22, 0.70])
    proposed_inset.set_facecolor(AX_BG)
    proposed_boxplot = proposed_inset.boxplot(
        [proposed_values], positions=[1], widths=0.48,
        patch_artist=True, showfliers=False, manage_ticks=False,
    )
    style_boxplot(proposed_boxplot, [SCHEME_COLORS[2]])
    proposed_inset.set_xlim(0.65, 1.35)
    proposed_inset.set_ylim(
        proposed_min - proposed_padding,
        proposed_max + proposed_padding,
    )
    proposed_inset.set_xticks([])
    proposed_inset.yaxis.set_major_locator(mpl.ticker.MaxNLocator(nbins=3))
    proposed_inset.yaxis.set_major_formatter(tick_label_formatter(decimals=3))
    proposed_inset.tick_params(
        axis='y', direction='in', right=False,
        labelsize=FONT_TICK * 0.62, length=2,
    )
    proposed_inset.tick_params(axis='x', top=False, bottom=False)
    proposed_inset.grid(
        axis='y', linestyle='--', linewidth=0.4, alpha=0.55,
        color=GRID_COLOR,
    )
    for spine in proposed_inset.spines.values():
        spine.set_color(CARD_EDGE)
        spine.set_linewidth(0.8)
    proposed_inset.set_title(
        'Our Proposal zoom', fontsize=FONT_TEXT * 0.86, pad=2,
    )

    ax_high.spines['bottom'].set_visible(False)
    ax_low.spines['top'].set_visible(False)
    ax_high.tick_params(axis='x', which='both', bottom=False, labelbottom=False)
    ax_low.tick_params(axis='x', which='both', top=False)
    ax_low.set_xticks([1, 2, 3])
    ax_low.set_xticklabels(['1', '2', '3'])
    ax_low.set_xlabel('Scheduling scheme', fontsize=FONT_LABEL)

    # Draw diagonal marks at both sides of the y-axis break.
    break_size = 0.012
    high_break_style = dict(transform=ax_high.transAxes, color=CARD_EDGE,
                            clip_on=False, linewidth=1.0)
    ax_high.plot((-break_size, +break_size), (-break_size, +break_size),
                 **high_break_style)
    ax_high.plot((1 - break_size, 1 + break_size), (-break_size, +break_size),
                 **high_break_style)
    low_break_style = dict(transform=ax_low.transAxes, color=CARD_EDGE,
                           clip_on=False, linewidth=1.0)
    ax_low.plot((-break_size, +break_size), (1 - break_size, 1 + break_size),
                **low_break_style)
    ax_low.plot((1 - break_size, 1 + break_size),
                (1 - break_size, 1 + break_size), **low_break_style)

    # Same wording as the 2TT jitter legend (All delivered packets /
    # Never retransmitted) so the two scenario sets read identically.
    suffix = 'All delivered packets' if include_retransmitted else 'Never retransmitted'
    ax_high.set_title(f'Jitter\n({suffix})', fontsize=FONT_TITLE)
    fig.supylabel(metric_ylabel, fontsize=FONT_LABEL, x=0.022)

    legend_handles = [
        Patch(facecolor=SCHEME_COLORS[0], edgecolor=CARD_EDGE, label='1: Best effort'),
        Patch(facecolor=SCHEME_COLORS[1], edgecolor=CARD_EDGE, label='2: Ground Gated'),
        Patch(facecolor=SCHEME_COLORS[2], edgecolor=CARD_EDGE, label='3: Our Proposal'),
    ]
    leg = ax_high.legend(
        handles=legend_handles, loc='upper left', ncol=1,
        bbox_to_anchor=(0.02, 0.98),
        fontsize=FONT_LEGEND * 0.87,
        handlelength=1.50, handletextpad=0.50,
        borderpad=0.40, labelspacing=0.34,
        frameon=True, fancybox=True,
    )
    style_leg(leg)
    # Same canvas as the 2TT figures; the left margin is wider than the shared
    # default because the shared y-axis label plus "40,000"-style ticks need it.
    fig.subplots_adjust(left=0.13, right=0.97, bottom=0.15, top=0.88, hspace=0.07)
    save_figure(fig, filename_stem)


# ── arrival rate ─────────────────────────────────────────────────────────

def plot_arrival_rates():
    for s_idx, sdir in enumerate(SCHEME_DIRS):
        theory = read_ideal_packet_counts(
            sdir, ALL_FLOWS, generation_cutoff_s=TS_GENERATION_CUTOFF_S
        )
        actual = np.zeros(29, dtype=int)
        for fid in ALL_FLOWS:
            path = os.path.join(sdir, f'flow{fid}.csv')
            if not os.path.exists(path): continue
            df = pd.read_csv(path)
            if fid <= 9:
                actual[fid - 1] = len(df)
            else:
                # Apply the same generation-time window to numerator and
                # denominator. A retransmission delivered after the cutoff is
                # still counted when its original packet was generated by it.
                original_creation_time = df['simTime'] - df['endToEndDelay']
                df = df[
                    original_creation_time
                    <= TS_GENERATION_CUTOFF_S + TS_GENERATION_CUTOFF_EPSILON_S
                ]
                if 'outcome' in df.columns:
                    df = df[df['outcome'] == 'delivered']
                # A delivered sequence is counted once regardless of which
                # transmission attempt ultimately delivered it.
                df = df.drop_duplicates(subset='sequenceNumber')
                actual[fid - 1] = len(df)

        # Same 18 x 15 cm canvas as the other 20TT figures. The arrival plots
        # have two y axes, so reserve only the space their labels need and give
        # the central plotting region the remaining width.
        fig, ax1 = plt.subplots(figsize=FIG_SIZE_WIDE)
        add_bg(ax1)
        ax2 = ax1.twinx()
        flow_ids = np.arange(1, 30)
        w = 0.5

        # Non-TS flows use the left linear axis; TS flows use the right linear
        # axis because their packet counts are orders of magnitude smaller.
        ax1.bar(flow_ids[:9], actual[:9].astype(float), w, label='Actual', color=COLOR_ACTUAL,
                edgecolor=CARD_EDGE, linewidth=0.8)
        ax1.bar(flow_ids[:9], (np.array(theory[:9], dtype=float) - actual[:9]), w,
                bottom=actual[:9].astype(float), label='Ideal', color=COLOR_THEORY,
                edgecolor=CARD_EDGE, linewidth=0.8)
        ax2.bar(flow_ids[9:29], actual[9:29].astype(float), w, color=COLOR_ACTUAL,
                edgecolor=CARD_EDGE, linewidth=0.8)
        ax2.bar(flow_ids[9:29], (np.array(theory[9:29], dtype=float) - actual[9:29]), w,
                bottom=actual[9:29].astype(float), color=COLOR_THEORY,
                edgecolor=CARD_EDGE, linewidth=0.8)

        non_ts_max = float(max(actual[:9].max(), theory[:9].max()))
        ts_max = float(max(actual[9:].max(), theory[9:].max()))
        ax1.set_yscale('linear')
        ax2.set_yscale('linear')
        ax1.set_ylim(0, non_ts_max * 1.30)
        ax2.set_ylim(0, ts_max * 1.30)
        non_ts_formatter = mpl.ticker.ScalarFormatter(useMathText=True)
        non_ts_formatter.set_scientific(True)
        non_ts_formatter.set_powerlimits((5, 5))
        non_ts_formatter.set_useOffset(False)
        ax1.yaxis.set_major_formatter(non_ts_formatter)
        ts_formatter = mpl.ticker.ScalarFormatter(useMathText=True)
        ts_formatter.set_scientific(True)
        ts_formatter.set_powerlimits((0, 0))
        ts_formatter.set_useOffset(False)
        ax2.yaxis.set_major_formatter(ts_formatter)
        ax1.yaxis.set_major_locator(mpl.ticker.MaxNLocator(nbins=6, integer=True))
        ax2.yaxis.set_major_locator(mpl.ticker.MaxNLocator(nbins=6, integer=True))
        ax1.set_xlim(0.5, 29.5)

        for i in range(9):
            pct = actual[i] / theory[i] * 100 if theory[i] > 0 else 0
            ax1.text(flow_ids[i], theory[i] * 1.02, f'{pct:.3f}%', ha='center', va='bottom',
                     fontsize=FONT_TEXT * 0.57, clip_on=False, rotation=90)
        for i in range(9, 29):
            pct = actual[i] / theory[i] * 100 if theory[i] > 0 else 0
            ax2.text(flow_ids[i], theory[i] * 1.02, f'{pct:.3f}%', ha='center', va='bottom',
                     fontsize=FONT_TEXT * 0.57, clip_on=False, rotation=90)

        ax1.set_title(SCHEME_LABELS[s_idx] + ' per-flow arrival rate',
                      fontsize=FONT_TITLE)
        ax1.set_xlabel('Flow ID', fontsize=FONT_LABEL)
        ax1.set_ylabel('Package Count (flows 1–9)', fontsize=FONT_LABEL)
        ax2.set_ylabel('Package Count (flows 10–29)', fontsize=FONT_LABEL)
        ax1.set_xticks(flow_ids)
        ax1.set_xticklabels(
            flow_ids, rotation=0, ha='center', fontsize=FONT_TICK,
        )
        style_ax(ax1)
        ax2.tick_params(axis='both', labelsize=FONT_TICK)
        ax2.grid(False)
        for spine in ax2.spines.values():
            spine.set_color(CARD_EDGE); spine.set_linewidth(1.0)
        legend_elems = [Patch(facecolor=COLOR_ACTUAL, edgecolor=CARD_EDGE, label='Actual'),
                        Patch(facecolor=COLOR_THEORY, edgecolor=CARD_EDGE, label='Ideal')]
        ax1.legend(handles=legend_elems, loc='upper center', ncol=2,
                   fontsize=FONT_LEGEND * 0.87,
                   frameon=True, fancybox=True)
        style_leg(ax1.get_legend())
        # Same visual margins as the 20-TT delay-distribution figures; the
        # right margin is narrower because these plots carry a second y axis.
        fig.subplots_adjust(left=0.09, right=0.91, bottom=0.15, top=0.89)
        sname = SCHEME_FILE_STEMS[SCHEME_LABELS[s_idx]]
        save_figure(fig, f'arrival_rate_{sname}_20tt')


def remove_obsolete_figures():
    """Delete outputs of earlier runs that are no longer produced."""
    obsolete = ['boxplot_jitter_rms_20tt*', 'boxplot_jitter_range_20tt*']
    removed = []
    for pattern in obsolete:
        for path in glob.glob(os.path.join(OUTPUT_DIR, pattern)):
            os.remove(path)
            removed.append(os.path.basename(path))
    return removed


def main():
    set_style()
    plot_boxplots()
    plot_delay_range_boxplot(include_retransmitted=True)
    plot_delay_range_boxplot(include_retransmitted=False)
    plot_arrival_rates()
    removed = remove_obsolete_figures()
    if removed:
        print(f'Removed obsolete figures: {", ".join(sorted(removed))}')
    n = len([f for f in os.listdir(OUTPUT_DIR) if f.endswith('.png')])
    print(f'Done. {n} figures in {OUTPUT_DIR}')


if __name__ == '__main__':
    main()
