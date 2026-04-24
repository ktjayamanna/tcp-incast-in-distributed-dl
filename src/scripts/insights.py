#!/usr/bin/env python3
"""
Three key figures for the results slide.

Figure 1 - Control drop rate: low / medium / high scenarios x 3 engines
           Low = 0% baseline (buffer never full), medium/high show GPU advantage

Figure 2 - Blind window vs batch size
           CPU introsort O(n log n) explodes; GPU radix sort O(d*n) stays flat

Figure 3 - Drop composition at high scenario, single stacked bar
           Same total pressure, GPU shifts drops from control (red) to bulk (grey)
"""
import os
import openpyxl
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

XLSX = 'data/results/evaluation.xlsx'
wb   = openpyxl.load_workbook(XLSX)

def sheet_rows(name):
    ws = wb[name]
    rows = list(ws.iter_rows(values_only=True))
    return [dict(zip(rows[0], r)) for r in rows[1:]]

summary  = sheet_rows('Summary')
sort_prf = sheet_rows('Sort Performance')
gpu_pipe = sheet_rows('GPU Pipeline')

def get(rows, scenario, engine, key):
    for r in rows:
        if r.get('Scenario') == scenario and r.get('Engine') == engine:
            return r.get(key)
    return None

OUT_DIR = 'data/results/figures'
os.makedirs(OUT_DIR, exist_ok=True)

COLORS = {
    'CPU FIFO': '#9E9E9E',
    'CPU PQ':   '#FF8C00',
    'GPU PQ':   '#1565C0',
}

plt.rcParams.update({
    'font.family':        'DejaVu Sans',
    'font.size':          11,
    'axes.spines.top':    False,
    'axes.spines.right':  False,
    'axes.grid':          True,
    'axes.grid.axis':     'y',
    'grid.alpha':         0.35,
    'grid.linestyle':     '--',
})

# Buffer sizes matching config.py SCENARIO_BUFFER_BYTES
BUFFER_LABEL = {
    'low':    '50 MB buffer',
    'medium': '5 MB buffer',
    'high':   '16 MB buffer',
}
SENDER_LABEL = {
    'low':    '128 senders',
    'medium': '5k senders',
    'high':   '10k senders',
}

# =============================================================================
# Figure 1 - Control drop rate: low / medium / high
# =============================================================================

SCENARIOS = ['low', 'medium', 'high']
ENGINES   = ['CPU FIFO', 'CPU PQ', 'GPU PQ']

fig1, ax1 = plt.subplots(figsize=(8.5, 4.8))

x     = np.arange(len(SCENARIOS))
width = 0.22

for ei, eng in enumerate(ENGINES):
    rates = [get(summary, sc, eng, 'Ctrl Drop %') or 0 for sc in SCENARIOS]
    offset = (ei - 1) * width
    bars = ax1.bar(x + offset, rates, width, color=COLORS[eng],
                   label=eng, zorder=3, edgecolor='white', linewidth=0.5)
    for bar, rate in zip(bars, rates):
        if rate >= 1:
            ax1.text(bar.get_x() + bar.get_width() / 2,
                     bar.get_height() + 0.8,
                     f'{rate:.0f}%', ha='center', va='bottom',
                     fontsize=8.5, fontweight='bold', color=COLORS[eng])
        elif rate == 0:
            ax1.text(bar.get_x() + bar.get_width() / 2,
                     0.8, '0%', ha='center', va='bottom',
                     fontsize=8, color='#666666')

xlabels = [
    f'{SENDER_LABEL[sc]}\n{BUFFER_LABEL[sc]}'
    for sc in SCENARIOS
]
ax1.set_xticks(x)
ax1.set_xticklabels(xlabels)
ax1.set_ylabel('Control packet drop rate (%)')
ax1.set_title('GPU priority sorting cuts control packet drops at scale',
              fontweight='bold', pad=10)
ax1.set_ylim(0, 78)
ax1.legend(framealpha=0.9, loc='upper left')

# Annotate GPU advantage on high scenario
gpu_high = get(summary, 'high', 'GPU PQ',  'Ctrl Drop %')
cpu_high = get(summary, 'high', 'CPU PQ',  'Ctrl Drop %')
pct_red  = (cpu_high - gpu_high) / cpu_high * 100
ax1.annotate(f'{pct_red:.0f}% fewer\nctrl drops vs CPU PQ',
             xy=(x[2] + width, gpu_high),
             xytext=(x[2] + width + 0.32, gpu_high + 15),
             fontsize=8.5, color=COLORS['GPU PQ'],
             arrowprops=dict(arrowstyle='->', color=COLORS['GPU PQ'], lw=1.4))

# Annotate the low scenario baseline meaning
ax1.text(x[0], 5.5, 'No congestion\nbaseline', ha='center',
         fontsize=7.5, color='#555555', style='italic')

fig1.tight_layout()
fig1.savefig(f'{OUT_DIR}/fig1_ctrl_drop_rate.png', dpi=150, bbox_inches='tight')
plt.close(fig1)
print('Saved fig1_ctrl_drop_rate.png')

# =============================================================================
# Figure 2 - Blind window vs batch size
# =============================================================================

cpu_pts, gpu_pts = [], []
for sc in ['low', 'medium', 'high']:
    batch = next((r['Avg Batch Size'] for r in gpu_pipe if r.get('Scenario') == sc), None)
    cpu_bw = get(sort_prf, sc, 'CPU PQ', 'Avg Blind Window (µs)')
    gpu_bw = get(sort_prf, sc, 'GPU PQ', 'Avg Blind Window (µs)')
    if batch and cpu_bw:
        cpu_pts.append((batch, cpu_bw))
    if batch and gpu_bw:
        gpu_pts.append((batch, gpu_bw))

cpu_pts.sort(); gpu_pts.sort()
cpu_x, cpu_y = zip(*cpu_pts)
gpu_x, gpu_y = zip(*gpu_pts)

fig2, ax2 = plt.subplots(figsize=(7.5, 4.5))

ax2.plot(cpu_x, cpu_y, 'o-', color=COLORS['CPU PQ'], lw=2.2,
         markersize=7, label='CPU PQ  (introsort, O(n log n))')
ax2.plot(gpu_x, gpu_y, 's-', color=COLORS['GPU PQ'], lw=2.2,
         markersize=7, label='GPU PQ  (radix sort, O(d*n))')

ratio = cpu_y[-1] / gpu_y[-1]
ax2.annotate(f'{ratio:.0f}x shorter\nblind window',
             color=COLORS['GPU PQ'],
             xy=(gpu_x[-1], gpu_y[-1]),
             xytext=(gpu_x[-1] * 0.58, gpu_y[-1] + 620),
             fontsize=9, fontweight='bold',
             arrowprops=dict(arrowstyle='->', color=COLORS['GPU PQ'], lw=1.4))

ax2.set_xlabel('Avg packets in buffer per sort epoch')
ax2.set_ylabel('Blind window (us)')
ax2.set_title('CPU blind window explodes with buffer size; GPU stays flat',
              fontweight='bold', pad=10)
ax2.legend(framealpha=0.9)

fig2.tight_layout()
fig2.savefig(f'{OUT_DIR}/fig2_blind_window_scaling.png', dpi=150, bbox_inches='tight')
plt.close(fig2)
print('Saved fig2_blind_window_scaling.png')

# =============================================================================
# Figure 3 - Drop composition (high), single stacked bar
# =============================================================================

fig3, ax3 = plt.subplots(figsize=(7, 5.0))

eng_labels = ['CPU FIFO', 'CPU PQ', 'GPU PQ']
ctrl_drops = [get(summary, 'high', e, 'Ctrl Dropped') or 0 for e in eng_labels]
bulk_drops = [get(summary, 'high', e, 'Bulk Dropped') or 0 for e in eng_labels]
totals     = [c + b for c, b in zip(ctrl_drops, bulk_drops)]

x3 = np.arange(len(eng_labels))
w  = 0.48

BULK_COLOR = '#1E88E5'
CTRL_COLOR = '#E53935'

ax3.bar(x3, bulk_drops, w, label='Bulk dropped',
        color=BULK_COLOR, zorder=3, edgecolor='white')
ax3.bar(x3, ctrl_drops, w, bottom=bulk_drops,
        label='Control dropped', color=CTRL_COLOR, zorder=3, edgecolor='white')

Y_MIN  = min(bulk_drops) * 0.96          # zoom: start just below shortest bulk bar
Y_TOP  = max(totals) * 1.005 + 18_000   # headroom for labels

for xi, (cd, bd, tot) in enumerate(zip(ctrl_drops, bulk_drops, totals)):
    label_gap = (Y_TOP - Y_MIN) * 0.015

    # Bulk count: centre of blue segment (in data coords)
    bulk_mid = Y_MIN + (bd - Y_MIN) / 2
    ax3.text(xi, bulk_mid, f'bulk\n{bd:,}',
             ha='center', va='center', fontsize=8.5,
             color='white', fontweight='bold')

    seg_frac = cd / tot
    if seg_frac > 0.02:
        ax3.text(xi, bd + cd / 2, f'ctrl\n{cd:,}',
                 ha='center', va='center', fontsize=8.5,
                 color='white', fontweight='bold')
    else:
        # Tiny red cap (GPU PQ): label just above bar in red
        ax3.text(xi, tot + label_gap * 0.5, f'ctrl: {cd:,}',
                 ha='center', va='bottom', fontsize=8,
                 color=CTRL_COLOR, fontweight='bold')

    # Total above each bar (extra gap for GPU PQ to clear ctrl label)
    extra = label_gap * 3.5 if seg_frac <= 0.02 else label_gap
    ax3.text(xi, tot + extra, f'total: {tot:,}',
             ha='center', va='bottom', fontsize=8, color='#333333')

ax3.set_xticks(x3)
ax3.set_xticklabels(eng_labels, fontsize=11)
ax3.set_ylabel('Packets dropped')
ax3.set_title('High scenario: GPU shifts drops from control to bulk',
              fontweight='bold', pad=12)
ax3.legend(framealpha=0.95, loc='upper center',
           bbox_to_anchor=(0.5, 1.0), ncol=2,
           fontsize=9, handlelength=1.4)
ax3.set_ylim(Y_MIN, Y_TOP)

# Break indicator at bottom of y-axis (zigzag lines)
d = 0.012
kwargs = dict(transform=ax3.transAxes, color='#888888', clip_on=False, lw=1.2)
ax3.plot((-d, +d), (-d*0.5,  d*0.5), **kwargs)
ax3.plot((-d, +d), ( d*0.5, -d*0.5+0.022), **kwargs)

fig3.tight_layout()
fig3.savefig(f'{OUT_DIR}/fig3_drop_composition_high.png', dpi=150, bbox_inches='tight')
plt.close(fig3)
print('Saved fig3_drop_composition_high.png')

print()
print('Key numbers:')
for sc in ['low', 'medium', 'high']:
    cpu = get(summary, sc, 'CPU PQ', 'Ctrl Drop %') or 0
    gpu = get(summary, sc, 'GPU PQ', 'Ctrl Drop %') or 0
    cpu_bw = get(sort_prf, sc, 'CPU PQ', 'Avg Blind Window (µs)') or 0
    gpu_bw = get(sort_prf, sc, 'GPU PQ', 'Avg Blind Window (µs)') or 0
    ratio  = cpu_bw / gpu_bw if gpu_bw else 0
    print(f'  {sc:8s}  ctrl: CPU {cpu:.1f}% -> GPU {gpu:.1f}%   '
          f'blind window: CPU {cpu_bw:.0f}us vs GPU {gpu_bw:.0f}us  ({ratio:.0f}x shorter)')
