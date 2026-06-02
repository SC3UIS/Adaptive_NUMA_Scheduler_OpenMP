
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

hilos       = [8, 16, 32, 64, 128]

gibs_no     = [78.716, 149.459, 219.564, 239.327, 265.865]
gflops_no   = [13.353, 25.359,  37.252,  40.608,  45.114]
tmin_no     = [52.3189, 27.5506, 18.755, 17.204, 15.486]

gibs_si     = [79.154, 150.103, 170.015, 170.58, 250.022]
gflops_si   = [13.431, 25.468,  28.789,  28.919, 42.424]
tmin_si     = [52.016, 27.4323, 24.266, 24.1571, 16.468]

migraciones = [7, 16, 26, None, 76]  

overhead = [(si - no) / no * 100 for si, no in zip(tmin_si, tmin_no)]

plt.rcParams.update({
    "font.family":      "sans-serif",
    "font.sans-serif":  ["Segoe UI", "Arial", "Helvetica", "DejaVu Sans"],
    "font.size":        11,
    "axes.titlesize":   14,
    "axes.titleweight":  "bold",
    "axes.labelsize":   12,
    "axes.grid":        True,
    "grid.alpha":       0.3,
    "grid.linestyle":   "--",
    "legend.fontsize":  10,
    "figure.dpi":       150,
    "savefig.dpi":      200,
    "savefig.bbox":     "tight",
})

COLOR_NO = "#2196F3"  
COLOR_SI = "#F44336"   
MARKER_NO = "o"
MARKER_SI = "s"

x_ticks = hilos


fig1, ax1 = plt.subplots(figsize=(9, 5.5))

ax1.plot(hilos, gibs_no, color=COLOR_NO, marker=MARKER_NO, linewidth=2.2,
         markersize=8, label="Without Scheduler", zorder=3)
ax1.plot(hilos, gibs_si, color=COLOR_SI, marker=MARKER_SI, linewidth=2.2,
         markersize=8, label="With Scheduler", zorder=3)

for x, y_no, y_si in zip(hilos, gibs_no, gibs_si):
    ax1.annotate(f"{y_no:.1f}", (x, y_no), textcoords="offset points",
                 xytext=(0, 14), ha="center", fontsize=10, color=COLOR_NO,
                 fontweight="bold", bbox=dict(facecolor='white', edgecolor='none', alpha=0.8, pad=1.5))
    ax1.annotate(f"{y_si:.1f}", (x, y_si), textcoords="offset points",
                 xytext=(0, -18), ha="center", fontsize=10, color=COLOR_SI,
                 fontweight="bold", bbox=dict(facecolor='white', edgecolor='none', alpha=0.8, pad=1.5))

ax1.set_xlabel("Number of Threads")
ax1.set_ylabel("Bandwidth (GiB/s)")
ax1.set_title("SpMV Performance — Bandwidth (GiB/s)")
ax1.set_xticks(x_ticks)
ax1.set_xticklabels([str(h) for h in x_ticks])
ax1.legend(loc="upper left", framealpha=0.9)
ax1.set_xlim(0, 136)
ax1.set_ylim(0, max(max(gibs_no), max(gibs_si)) * 1.15)

fig1.tight_layout()
fig1.savefig("plot_gibs.png")
fig1.savefig("plot_gibs.eps", format='eps')
print("[OK] Saved: plot_gibs.png and plot_gibs.eps")

fig2, ax2 = plt.subplots(figsize=(9, 5.5))

ax2.plot(hilos, gflops_no, color=COLOR_NO, marker=MARKER_NO, linewidth=2.2,
         markersize=8, label="Without Scheduler", zorder=3)
ax2.plot(hilos, gflops_si, color=COLOR_SI, marker=MARKER_SI, linewidth=2.2,
         markersize=8, label="With Scheduler", zorder=3)

for x, y_no, y_si in zip(hilos, gflops_no, gflops_si):
    ax2.annotate(f"{y_no:.2f}", (x, y_no), textcoords="offset points",
                 xytext=(0, 14), ha="center", fontsize=10, color=COLOR_NO,
                 fontweight="bold", bbox=dict(facecolor='white', edgecolor='none', alpha=0.8, pad=1.5))
    ax2.annotate(f"{y_si:.2f}", (x, y_si), textcoords="offset points",
                 xytext=(0, -18), ha="center", fontsize=10, color=COLOR_SI,
                 fontweight="bold", bbox=dict(facecolor='white', edgecolor='none', alpha=0.8, pad=1.5))

ax2.set_xlabel("Number of Threads")
ax2.set_ylabel("Performance (GFLOPS)")
ax2.set_title("SpMV Performance — GFLOPS")
ax2.set_xticks(x_ticks)
ax2.set_xticklabels([str(h) for h in x_ticks])
ax2.legend(loc="upper left", framealpha=0.9)
ax2.set_xlim(0, 136)
ax2.set_ylim(0, max(max(gflops_no), max(gflops_si)) * 1.15)

fig2.tight_layout()
fig2.savefig("plot_gflops.png")
fig2.savefig("plot_gflops.eps", format='eps')
print("[OK] Saved: plot_gflops.png and plot_gflops.eps")


fig3, ax3 = plt.subplots(figsize=(9, 5.5))

x_pos = np.arange(len(hilos))
bar_w = 0.55

COLOR_SPEEDUP  = "#4CAF50"  
COLOR_OVERHEAD = "#F44336"   
bar_colors = [COLOR_SPEEDUP if v < 0 else COLOR_OVERHEAD for v in overhead]

bars = ax3.bar(x_pos, overhead, bar_w, color=bar_colors,
               edgecolor="white", linewidth=0.8, zorder=3)

ax3.axhline(y=0, color="#555555", linewidth=1.0, linestyle="-", zorder=2)

for i, (bar, val) in enumerate(zip(bars, overhead)):
    sign = "+" if val > 0 else ""
    y_offset = 8 if val >= 0 else -14
    ax3.annotate(f"{sign}{val:.2f}%", (bar.get_x() + bar.get_width()/2, val),
                 textcoords="offset points", xytext=(0, y_offset), ha="center",
                 fontsize=10, fontweight="bold",
                 color=bar_colors[i])

y_max = max(overhead)
y_min = min(overhead)


from matplotlib.patches import Patch
legend_elements = [
    Patch(facecolor=COLOR_OVERHEAD, edgecolor="white", label="Overhead (slower with scheduler)"),
    Patch(facecolor=COLOR_SPEEDUP,  edgecolor="white", label="Speedup (faster with scheduler)"),
]
ax3.legend(handles=legend_elements, loc="upper left", framealpha=0.9)

ax3.set_xlabel("Number of Threads")
ax3.set_ylabel("Overhead (%)")
ax3.set_title("NUMA Scheduler Overhead")
ax3.set_xticks(x_pos)
ax3.set_xticklabels([str(h) for h in hilos])

margin = max(abs(y_max), abs(y_min)) * 0.35
ax3.set_ylim(y_min - margin, y_max + margin)

fig3.tight_layout()
fig3.savefig("plot_overhead.png")
fig3.savefig("plot_overhead.eps", format='eps')
print("[OK] Saved: plot_overhead.png and plot_overhead.eps")

plt.show()
print("\n[OK] All plots have been generated correctly.")
