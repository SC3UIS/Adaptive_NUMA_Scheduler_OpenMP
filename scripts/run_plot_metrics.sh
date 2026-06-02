#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTDIR="${1:-${ROOT_DIR}/data}"

python3 "${ROOT_DIR}/scripts/plot_numa_results.py"
  --outdir "${OUTDIR}" \
  --figdir "${ROOT_DIR}/plots"
