# Mitigating NUMA Penalties: An OMPT-Driven Adaptive Scheduler for OpenMP

This repository contains an OMPT-based adaptive NUMA scheduler, a sparse matrix-vector multiplication (SpMV) benchmark, and this repository contains an OMPT-based adaptive NUMA scheduler, a sparse matrix-vector multiplication (SpMV) benchmark, and the tooling required to reproduce the experiments and figures presented in this project.

## Overview


The project provides an experimental framework for studying the impact of adaptive thread scheduling on NUMA systems using OpenMP Tools (OMPT), hardware performance monitoring, and thread migration policies.

Performance is reported using:

Minimum execution time (t_min)
Standard deviation (stdev_ms)
Memory bandwidth (GiB/s)
Computational throughput (GFLOPS)

Results are exported in CSV format and visualized through automatically generated plots.

## End-to-End Workflow
1. Place `stokes.mtx` in the repository root (or set `SPMV_MTX` to its path).
2. Run `./scripts/run_perf_metricsV4.sh` to generate `spread_interleave_all.csv`.
3. Run `./scripts/run_plot_metrics.sh` to regenerate the figures in `data/`.

## Quick Start
```
./scripts/run_perf_metricsV4.sh
```

## Installation
Install the required system packages for your platform:

- Clang with OpenMP support
- `hwloc` (headers and runtime)
- `numactl`
- Python 3 with `matplotlib` (for plots only)

## Recommended Order

1. Review the operational guide in `docs/` for the exact run procedure.
2. Inspect the scheduler implementation in `src/NUMA_scheduler_OMPT.cpp`.
3. Run the benchmarking scripts contained in `scripts/`.

## First Operational Smoke Run
```
SPMV_MTX=./stokes.mtx ./scripts/run_perf_metricsV4.sh
```

## Main Operational Entry Points
| Path | Purpose |
| --- | --- |
| `scripts/run_perf_metricsV4.sh` | Runs the baseline vs scheduler experiments and writes `spread_interleave_all.csv`. |
| `scripts/run_plot_metrics.sh` | Generates plots in `data/` from `spread_interleave_all.csv`. |

## Key Outputs
| Output | Description |
| --- | --- |
| `data/spread_interleave_all.csv` | Final metrics: min time, stddev, GFLOP/s, GiB/s, overhead. |
| `data/ompt_logs/` | OMPT logs per run. |
| `data/plot_*.eps` | Figures referenced in the paper. |


## Repository Map

```text
.
├── src/                 # SpMV kernel and adaptive NUMA scheduler sources
├── scripts/             # Benchmark execution and plotting workflows
├── docs/                # Operational and technical documentation
├── data/                # Benchmark outputs, CSV datasets, and runtime logs
├── build/               # Generated binaries and shared libraries
├── plots/               # Generated figures and publication-ready graphics
├── stokes.mtx           # Input sparse matrix (user-provided)
└── README.md            # Project overview and usage instructions
```

## Documentation Map
| Document | Description |
| --- | --- |
| `docs/operational-guide.md` | Step-by-step operational procedure. |


## System Requirements
- Linux with NUMA support
- A multi-socket NUMA node (baseline target used in the paper)
- Sufficient memory for the Stokes matrix workload


## Contact

**Juan S. Otero V.** (ORCID: 0009-0004-9870-538X),
**David F. Naranjo R.** (ORCID: 0009-0004-6721-2654),
**Luis A. Torres N.** (ORCID: 0000-0003-2597-9430),
**Sergio A. Gélvez C.** (ORCID: 0000-0002-7898-3962), and
**Carlos J. Barrios H.** (ORCID: 0000-0002-3227-8651)

Universidad Industrial de Santander (UIS), Bucaramanga, Colombia
SC3UIS, CAGE, Bucaramanga, Colombia
LIG/INRIA-Grenoble, Saint Martin d’Hères, France
INSA Lyon, INRIA, CITI, UR3720, Villeurbanne, France

**Emails:**
[juan2220053@correo.uis.edu.co](mailto:juan2220053@correo.uis.edu.co), [david2220046@correo.uis.edu.co](mailto:david2220046@correo.uis.edu.co), [sergio.gelvez@correo.uis.edu.co](mailto:sergio.gelvez@correo.uis.edu.co), [luatorni@uis.edu.co](mailto:luatorni@uis.edu.co), [cbarrios@uis.edu.co](mailto:cbarrios@uis.edu.co)

