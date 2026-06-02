#!/usr/bin/env bash
set -euo pipefail


ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SPMV_MTX="${SPMV_MTX:-${ROOT_DIR}/stokes.mtx}"
SPMV_AVG_NNZ=360
REPS=100
THREAD_LIST=(8 16 32 64 128)

OUTDIR="${OUTDIR:-${ROOT_DIR}/data}"
TMPDIR="${OUTDIR}/tmp"
OMPT_LOGDIR="${OUTDIR}/ompt_logs"
SUMMARY_CSV="${OUTDIR}/spread_interleave_all.csv"

SRC_DIR="${ROOT_DIR}/src"
BUILD_DIR="${ROOT_DIR}/build"

CXX=clang++
CXXFLAGS=(-std=c++17 -O3 -fopenmp -ffast-math)

SPMV_SRC="${SRC_DIR}/SpMV_Kernel.cpp"
SPMV_BIN="${BUILD_DIR}/spmv_static"

OMPT_TOOL_SRC="${SRC_DIR}/NUMA_scheduler_OMPT.cpp"
OMPT_TOOL_BIN="${BUILD_DIR}/numa_sched_finalratiov4.so"
OMPT_TOOL_FLAGS=(-std=c++17 -fPIC -shared -fopenmp -pthread -O2)
HWLOC_INCLUDE="${HWLOC_INCLUDE:-/opt/ohpc/pub/libs/hwloc/include}"
HWLOC_LIB="${HWLOC_LIB:-/opt/ohpc/pub/libs/hwloc/lib}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "ERROR: Falta comando requerido: $1" >&2
    exit 1
  }
}

mkdirs() {
  mkdir -p "${OUTDIR}" "${TMPDIR}" "${OMPT_LOGDIR}" "${BUILD_DIR}"
}

compile_kernel() {
  local label="$1"
  local src="$2"
  local bin="$3"

  if [[ ! -f "$src" ]]; then
    echo "[!] ERROR: source $src not found for $label" >&2
    return 1
  fi

  echo "[*] Compiling $label: $src -> $bin"
  "${CXX}" "${CXXFLAGS[@]}" "$src" -o "$bin"

  if [[ ! -x "$bin" ]]; then
    echo "[!] ERROR: compilation failed or binary not executable: $bin" >&2
    return 1
  fi
}

compile_ompt_tool() {
  if [[ ! -f "$OMPT_TOOL_SRC" ]]; then
    echo "[!] ERROR: OMPT tool source not found: $OMPT_TOOL_SRC" >&2
    exit 1
  fi

  echo "[*] Compiling OMPT tool: $OMPT_TOOL_SRC -> $OMPT_TOOL_BIN"
  "${CXX}" "${OMPT_TOOL_FLAGS[@]}" "$OMPT_TOOL_SRC" \
    -o "$OMPT_TOOL_BIN" \
    -I"${HWLOC_INCLUDE}" -L"${HWLOC_LIB}" -lhwloc

  if [[ ! -f "$OMPT_TOOL_BIN" ]]; then
    echo "[!] ERROR: OMPT tool compilation failed: $OMPT_TOOL_BIN" >&2
    exit 1
  fi
}

extract_kernel_metrics() {
  local csv="$1"
  awk -F',' '
    NR==1 { for (i=1;i<=NF;i++) idx[$i]=i; next }
    NR==2 {
      printf "%s %s %s %s\n", $(idx["min_ms"]), $(idx["stddev_ms"]), $(idx["gflops"]), $(idx["bw_gibs"]);
      exit
    }' "$csv"
}

compute_overhead() {
  local t_ms="$1"
  local base_ms="$2"
  awk -v t="$t_ms" -v b="$base_ms" 'BEGIN { if (b > 0) printf "%.6f", (t - b) / b * 100; else printf "0.000000" }'
}

need_cmd "${CXX}"
need_cmd numactl

mkdirs
compile_ompt_tool
compile_kernel "spmv_static" "$SPMV_SRC" "$SPMV_BIN"

export SPMV_AVG_NNZ="${SPMV_AVG_NNZ}"

rm -f "$SUMMARY_CSV"
echo "Configuration,Threads,Scheduler,t_min,stdev_ms,GiB/s,GFLOPS" > "$SUMMARY_CSV"

unset OMP_TOOL OMP_TOOL_LIBRARIES OMPT_LOG_FILE OMPT_TAG

declare -A BASE_MIN_MS

echo "[*] Stage 1/2: Baseline estatico (OMP_PROC_BIND=spread)"
for threads in "${THREAD_LIST[@]}"; do
  tag="spmv_t${threads}_baseline"
  csv_prefix="${TMPDIR}/${tag}"

  export OMP_NUM_THREADS="${threads}"
  export OMP_DYNAMIC="FALSE"
  export OMP_PLACES="cores"
  export OMP_PROC_BIND="spread"

  numactl --interleave=all -- "${SPMV_BIN}" "${SPMV_MTX}" "${threads}" "${REPS}" "${csv_prefix}" \
    >/dev/null 2>/dev/null || true

  read -r min_ms stddev_ms gflops bw_gibs <<< "$(extract_kernel_metrics "${csv_prefix}.csv")"
  rm -f "${csv_prefix}.csv"

  BASE_MIN_MS["${threads}"]="${min_ms}"
  printf "Spread and interleave all,%s,NO,%s,%s,%s,%s\n" \
    "${threads}" "${min_ms}" "${stddev_ms}" "${bw_gibs}" "${gflops}" >> "$SUMMARY_CSV"
done

echo ""
echo "[*] Stage 2/2: Scheduler OMPT (sin afinidad estatica)"
for threads in "${THREAD_LIST[@]}"; do
  tag="spmv_t${threads}_ompt"
  csv_prefix="${TMPDIR}/${tag}"
  ompt_log="${OMPT_LOGDIR}/${tag}.log"

  export OMP_NUM_THREADS="${threads}"
  export OMP_DYNAMIC="FALSE"
  export OMP_PLACES="cores"
  export OMP_PROC_BIND="spread"

  export OMP_TOOL="enabled"
  export OMP_TOOL_LIBRARIES="$(realpath "$OMPT_TOOL_BIN")"
  export OMPT_LOG_FILE="${ompt_log}"
  export OMPT_TAG="${tag}"

  numactl --interleave=all -- "${SPMV_BIN}" "${SPMV_MTX}" "${threads}" "${REPS}" "${csv_prefix}" \
    >/dev/null 2>/dev/null || true

  read -r min_ms stddev_ms gflops bw_gibs <<< "$(extract_kernel_metrics "${csv_prefix}.csv")"
  rm -f "${csv_prefix}.csv"

  base_ms="${BASE_MIN_MS[${threads}]:-0}"
  overhead_pct="$(compute_overhead "$min_ms" "$base_ms")"
  printf "Spread and interleave all,%s,YES,%s,%s,%s,%s\n" \
    "${threads}" "${min_ms}" "${stddev_ms}" "${bw_gibs}" "${gflops}" >> "$SUMMARY_CSV"
done

rm -rf "${TMPDIR}"

echo ""
echo "[✓] Benchmark V4 completed."
echo "    Summary CSV: ${SUMMARY_CSV}"
echo "    Logs: ${OMPT_LOGDIR}/"
