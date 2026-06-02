#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <tuple>
#include <cctype>
#include <atomic>
#include <cstdint>  

#ifdef _OPENMP
#include <omp.h>
#endif

#include <dlfcn.h>


static int* g_mig_counter = nullptr;


using IndexType = int;
using ValueType = double;

static void init_mig_counter(const char* tool_path) {
    void* h = dlopen(tool_path, RTLD_NOW | RTLD_NOLOAD);
    if (!h) {
        g_mig_counter = nullptr;
        return;
    }

    g_mig_counter = (int*)dlsym(h, "numa_sched_migration_count");
}

static inline int read_mig_counter() {
    if (!g_mig_counter) return 0;
    return __atomic_load_n(g_mig_counter, __ATOMIC_RELAXED);
}


struct CsrMatrix {
    IndexType  num_rows{0};
    IndexType  num_cols{0};
    long long  nnz{0};

    std::vector<IndexType> row_ptrs;
    std::vector<IndexType> col_idxs;
    std::vector<ValueType> values;

    std::string source_file{"(generated)"};
};

struct Triplet {
    IndexType r;
    IndexType c;
    ValueType v;
};

static inline std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
    return s;
}

CsrMatrix load_matrix_market(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }

    std::string header;
    if (!std::getline(file, header)) {
        throw std::runtime_error("Empty file or missing header: " + filename);
    }

    std::string hdr = to_lower_copy(header);

    if (hdr.find("matrixmarket") == std::string::npos) {
        throw std::runtime_error("Invalid MatrixMarket header.");
    }
    if (hdr.find("coordinate") == std::string::npos) {
        throw std::runtime_error(
            "Only 'coordinate' (sparse) format is supported.");
    }
    if (hdr.find("complex") != std::string::npos) {
        throw std::runtime_error(
            "'complex' matrices are not supported.");
    }

    const bool is_pattern   = (hdr.find("pattern")   != std::string::npos);
    const bool is_symmetric = (hdr.find("symmetric") != std::string::npos) ||
                              (hdr.find("hermitian") != std::string::npos);
    const bool is_skew      = (hdr.find("skew")      != std::string::npos);

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line[0] == '%') continue;
        break;
    }
    if (line.empty()) {
        throw std::runtime_error("Dimension line M N nnz not found.");
    }

    IndexType M = 0, N = 0;
    long long nnz_file = 0;
    {
        std::istringstream ss(line);
        if (!(ss >> M >> N >> nnz_file)) {
            throw std::runtime_error("Could not read M N nnz.");
        }
    }
    if (M <= 0 || N <= 0 || nnz_file < 0) {
        throw std::runtime_error("Invalid dimensions or nnz.");
    }

    std::cout << "[MTX] Dimensions: " << M << " x " << N
              << "  declared nnz=" << nnz_file
              << (is_symmetric ? " [symmetric/hermitian]" : "")
              << (is_skew ? " [skew]" : "")
              << (is_pattern ? " [pattern]" : "")
              << "\n";

    std::vector<Triplet> entries;
    entries.reserve((size_t)std::max<long long>(1, nnz_file * (is_symmetric || is_skew ? 2 : 1)));

    long long entries_read = 0;
    IndexType r_in = 0, c_in = 0;
    ValueType v_in = 0;

    while (file >> r_in >> c_in) {
        if (!is_pattern) {
            if (!(file >> v_in)) {
                throw std::runtime_error(
                    "Error reading value at entry " + std::to_string(entries_read + 1));
            }
        } else {
            v_in = (ValueType)1;
        }

        IndexType r = r_in - 1;
        IndexType c = c_in - 1;

        if (r < 0 || r >= M || c < 0 || c >= N) {
            throw std::runtime_error("Index out of range at entry " +
                                     std::to_string(entries_read + 1));
        }

        entries.push_back({r, c, v_in});

        if (is_symmetric && r != c) {
            entries.push_back({c, r, v_in});
        }
        if (is_skew && r != c) {
            entries.push_back({c, r, (ValueType)(-v_in)});
        }

        ++entries_read;
    }

    std::cout << "[MTX] Entries read from file: " << entries_read << "\n";
    std::cout << "[MTX] COO entries (with expansion): " << entries.size() << "\n";

    std::sort(entries.begin(), entries.end(),
              [](const Triplet& a, const Triplet& b) {
                  if (a.r != b.r) return a.r < b.r;
                  return a.c < b.c;
              });

    std::vector<Triplet> coo;
    coo.reserve(entries.size());

    for (const auto& t : entries) {
        if (!coo.empty() && coo.back().r == t.r && coo.back().c == t.c) {
            coo.back().v += t.v;
        } else {
            coo.push_back(t);
        }
    }

    {
        size_t w = 0;
        for (size_t i = 0; i < coo.size(); ++i) {
            if (coo[i].v != (ValueType)0) {
                coo[w++] = coo[i];
            }
        }
        coo.resize(w);
    }

    std::cout << "[MTX] Effective NNZ (after dedup): " << coo.size() << "\n";

    CsrMatrix mat;
    mat.num_rows = M;
    mat.num_cols = N;
    mat.nnz      = (long long)coo.size();
    mat.source_file = filename;

    mat.row_ptrs.assign((size_t)M + 1, 0);
    mat.col_idxs.resize((size_t)mat.nnz);
    mat.values.resize((size_t)mat.nnz);

    for (const auto& t : coo) {
        mat.row_ptrs[(size_t)t.r + 1]++;
    }

    for (IndexType i = 0; i < M; ++i) {
        mat.row_ptrs[(size_t)i + 1] += mat.row_ptrs[(size_t)i];
    }

    assert(mat.row_ptrs[(size_t)M] == (IndexType)mat.nnz);

    #pragma omp parallel for schedule(static)
    for (IndexType i = 0; i <= M; ++i) {
        volatile IndexType x = mat.row_ptrs[(size_t)i];
        (void)x;
    }

    std::vector<IndexType> row_begin((size_t)M + 1, 0);
    {
        size_t k = 0;
        for (IndexType r = 0; r < M; ++r) {
            row_begin[(size_t)r] = (IndexType)k;
            while (k < coo.size() && coo[k].r == r) ++k;
        }
        row_begin[(size_t)M] = (IndexType)coo.size();
    }

    #pragma omp parallel for schedule(static)
    for (IndexType r = 0; r < M; ++r) {
        IndexType out = mat.row_ptrs[(size_t)r];
        for (IndexType k = row_begin[(size_t)r]; k < row_begin[(size_t)r + 1]; ++k) {
            mat.col_idxs[(size_t)out] = coo[(size_t)k].c;
            mat.values[(size_t)out]   = coo[(size_t)k].v;
            ++out;
        }
    }

    std::cout << "[MTX] CSR built successfully (parallel row-wise fill).\n";
    return mat;
}

CsrMatrix generate_random_matrix(IndexType rows, IndexType cols,
                                  int avg_nnz, unsigned seed = 42)
{
    CsrMatrix A;
    A.num_rows    = rows;
    A.num_cols    = cols;
    A.source_file = "(random-Poisson)";

    std::mt19937 gen(seed);
    std::poisson_distribution<> poisson(avg_nnz);

    std::cout << "[GEN] Computing row structure...\n";
    A.row_ptrs.resize(rows + 1, 0);
    for (IndexType i = 0; i < rows; ++i) {
        int nnz_row = std::max(1, (int)poisson(gen));
        A.row_ptrs[i + 1] = A.row_ptrs[i] + nnz_row;
    }
    A.nnz = A.row_ptrs[rows];

    std::cout << "[GEN] Total NNZ: " << A.nnz
              << "  (density: "
              << std::scientific << std::setprecision(3)
              << (double)A.nnz / ((double)rows * cols) * 100.0
              << " %)\n" << std::defaultfloat;

    A.col_idxs.resize(A.nnz);
    A.values.resize(A.nnz);

#pragma omp parallel for schedule(static)
    for (IndexType i = 0; i < rows; ++i) {
#ifdef _OPENMP
        std::mt19937 lgen(seed + (unsigned)(omp_get_thread_num() * 99991u + i));
#else
        std::mt19937 lgen(seed + (unsigned)i);
#endif
        std::uniform_int_distribution<IndexType> col_dist(0, cols - 1);
        std::uniform_real_distribution<ValueType> val_dist(0.0, 1.0);

        for (IndexType k = A.row_ptrs[i]; k < A.row_ptrs[i + 1]; ++k) {
            A.col_idxs[k] = col_dist(lgen);
            A.values[k]   = val_dist(lgen);
        }
    }
    return A;
}

void init_vector(std::vector<ValueType>& v, ValueType fill = -1.0)
{
    const IndexType n = static_cast<IndexType>(v.size());
    if (fill >= 0.0) {
#pragma omp parallel for schedule(static)
        for (IndexType i = 0; i < n; ++i) v[i] = fill;
        return;
    }

    static const unsigned base_seed = static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count() ^
        (unsigned)std::random_device{}()
    );

#pragma omp parallel for schedule(static)
    for (IndexType i = 0; i < n; ++i) {
#ifdef _OPENMP
        std::mt19937 lgen(base_seed + (unsigned)(omp_get_thread_num() * 99991u + i));
#else
        std::mt19937 lgen(base_seed + (unsigned)i);
#endif
        std::uniform_real_distribution<ValueType> d(0.0, 1.0);
        v[i] = d(lgen);
    }
}

void spmv_static(const CsrMatrix& A,
                  const std::vector<ValueType>& x,
                        std::vector<ValueType>& y)
{
    const IndexType* rp  = A.row_ptrs.data();
    const IndexType* ci  = A.col_idxs.data();
    const ValueType* val = A.values.data();
    const ValueType* xv  = x.data();
          ValueType* yv  = y.data();

#pragma omp parallel for schedule(static, 256)
    for (IndexType row = 0; row < A.num_rows; ++row) {
        ValueType sum = 0.0;
        for (IndexType k = rp[row]; k < rp[row + 1]; ++k)
            sum += val[k] * xv[ci[k]];
        yv[row] = sum;
    }
}

bool validate_result(const CsrMatrix& A,
                     const std::vector<ValueType>& x,
                     const std::vector<ValueType>& y,
                     double tol = 1e-10)
{
    std::vector<ValueType> y_ref(A.num_rows, 0.0);
    for (IndexType i = 0; i < A.num_rows; ++i) {
        ValueType sum = 0.0;
        for (IndexType k = A.row_ptrs[i]; k < A.row_ptrs[i + 1]; ++k)
            sum += A.values[k] * x[A.col_idxs[k]];
        y_ref[i] = sum;
    }

    int errors = 0;
    for (IndexType i = 0; i < A.num_rows; ++i) {
        double ref_abs = std::abs(y_ref[i]);
        double rel     = std::abs(y[i] - y_ref[i]) / (ref_abs + 1e-14);
        if (rel > tol) {
            ++errors;
            if (errors <= 3)
                std::cerr << "  [VAL] Error at row " << i
                          << ": calc=" << y[i]
                          << " ref=" << y_ref[i]
                          << " rel_err=" << rel << "\n";
        }
    }
    if (errors == 0) { std::cout << "  [VAL] PASSED\n"; return true; }
    std::cout << "  [VAL] FAILED (" << errors << " errors out of "
              << A.num_rows << " rows)\n";
    return false;
}

double compute_bandwidth_gibs(const CsrMatrix& mat, double elapsed_s)
{
    double bytes =
          static_cast<double>(mat.nnz)          * sizeof(ValueType)
        + static_cast<double>(mat.nnz)          * sizeof(IndexType)
        + static_cast<double>(mat.num_rows + 1) * sizeof(IndexType)
        + static_cast<double>(mat.num_cols)     * sizeof(ValueType)
        + static_cast<double>(mat.num_rows)     * sizeof(ValueType);
    return (bytes / (1024.0 * 1024.0 * 1024.0)) / elapsed_s;
}

double compute_bandwidth_gbs(const CsrMatrix& mat, double elapsed_s)
{
    double bytes =
          static_cast<double>(mat.nnz) * (sizeof(IndexType) + sizeof(ValueType))
        + static_cast<double>(mat.num_cols) * sizeof(ValueType);
    return (bytes / 1e9) / elapsed_s;
}

struct BenchmarkResult {
    const char* strategy{nullptr};
    double min_time_s{0};
    double avg_time_s{0};
    double max_time_s{0};
    double stddev_s{0};
    double gflops{0};
    double bandwidth_gibs{0};
    double bandwidth_gbs{0};
};

using SpMVFunc = void(*)(const CsrMatrix&,
                         const std::vector<ValueType>&,
                               std::vector<ValueType>&);

BenchmarkResult benchmark_spmv(const CsrMatrix& A,
                                const std::vector<ValueType>& x,
                                      std::vector<ValueType>& y,
                                const char* strategy_name,
                                SpMVFunc spmv_func,
                                int reps   = 10,
                                int warmup = 2)
{
    std::cout << "\n[BENCH] Strategy: " << strategy_name << "\n";

    for (int i = 0; i < warmup; ++i) spmv_func(A, x, y);

    std::vector<double> times;
    times.reserve(reps);
    
    std::vector<double> times_after_first_mig;
    times_after_first_mig.reserve(reps);
    
    bool first_mig_seen = (read_mig_counter() > 0);
    bool collect_from_next_rep = false;
    
    omp_control_tool(omp_control_tool_start, 1, nullptr);
    for (int r = 0; r < reps; ++r) {
        int before = read_mig_counter();
    
        auto t0 = std::chrono::high_resolution_clock::now();
        spmv_func(A, x, y);
        auto t1 = std::chrono::high_resolution_clock::now();
        
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        times.push_back(elapsed);
        
        int after = read_mig_counter();
        
        bool mig_happened = (after > before);
        
        if (!first_mig_seen && mig_happened) {
            first_mig_seen = true;
            collect_from_next_rep = true;
        } else if (collect_from_next_rep) {
            times_after_first_mig.push_back(elapsed);
            collect_from_next_rep = false;
        } else if (first_mig_seen) {
            times_after_first_mig.push_back(elapsed);
        }
    
        printf("  Rep %2d: %.4f ms %s%s\n",
               r, elapsed * 1e3,
               mig_happened ? "[mig-during-rep] " : "",
               (first_mig_seen && !collect_from_next_rep) ? "[post-first-mig]" : "");
    }
    omp_control_tool(omp_control_tool_pause, 1, nullptr);
    std::sort(times.begin(), times.end());
    double min_t = times.front();
    double max_t = times.back();
    double avg_t = std::accumulate(times.begin(), times.end(), 0.0) / reps;
    double var   = 0.0;
    for (double t : times) var += (t - avg_t) * (t - avg_t);
    double stddev = std::sqrt(var / reps);
    
    if (!times_after_first_mig.empty()) {
        auto tmp = times_after_first_mig;
        std::sort(tmp.begin(), tmp.end());
        double min_post = tmp.front();

        double gflops_post  = (2.0 * static_cast<double>(A.nnz)) / (min_post * 1e9);
        double bw_gibs_post = compute_bandwidth_gibs(A, min_post);

        printf("  [POST-FIRST-MIG] reps=%zu  min=%.4f ms  GFlops=%.3f  GiB/s=%.3f\n",
               times_after_first_mig.size(),
               min_post * 1e3, gflops_post, bw_gibs_post);
    } else {
        printf("  [POST-FIRST-MIG] No migration detected during benchmark\n");
    }

    double gflops  = (2.0 * static_cast<double>(A.nnz)) / (min_t * 1e9);
    double bw_gibs = compute_bandwidth_gibs(A, min_t);
    double bw_gbs  = compute_bandwidth_gbs(A, min_t);

    printf("  Time    : %.4f ms (min) | %.4f ms (avg) | %.4f ms (max) | stddev=%.4f ms\n",
           min_t*1e3, avg_t*1e3, max_t*1e3, stddev*1e3);
    printf("  GFlops  : %.3f\n", gflops);
    printf("  BW GiB/s: %.3f  |  BW GB/s: %.3f\n", bw_gibs, bw_gbs);

    BenchmarkResult res;
    res.strategy       = strategy_name;
    res.min_time_s     = min_t;
    res.avg_time_s     = avg_t;
    res.max_time_s     = max_t;
    res.stddev_s       = stddev;
    res.gflops         = gflops;
    res.bandwidth_gibs = bw_gibs;
    res.bandwidth_gbs  = bw_gbs;
    return res;
}

void export_csv(const std::string& filename,
                const CsrMatrix& A,
                const std::vector<BenchmarkResult>& results)
{
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "[CSV] Could not create: " << filename << "\n";
        return;
    }

    int threads = 1;
#ifdef _OPENMP
    threads = omp_get_max_threads();
#endif

    f << "strategy,matrix,rows,cols,nnz,threads,"
         "min_ms,avg_ms,max_ms,stddev_ms,gflops,bw_gibs,bw_gbs\n";

    for (const auto& r : results) {
        f << r.strategy       << ","
          << A.source_file    << ","
          << A.num_rows       << ","
          << A.num_cols       << ","
          << A.nnz            << ","
          << threads          << ","
          << std::fixed << std::setprecision(6)
          << r.min_time_s*1e3 << ","
          << r.avg_time_s*1e3 << ","
          << r.max_time_s*1e3 << ","
          << r.stddev_s*1e3   << ","
          << r.gflops         << ","
          << r.bandwidth_gibs << ","
          << r.bandwidth_gbs  << "\n";
    }
    std::cout << "[CSV] Results saved to: " << filename << "\n";
}

int main(int argc, char* argv[])
{
    std::string mtx_file   = "";
    int         num_threads = 4;
    int         reps        = 10;
    std::string csv_prefix  = "";

    if (argc >= 2) mtx_file    = argv[1];
    if (argc >= 3) num_threads  = std::stoi(argv[2]);
    if (argc >= 4) reps         = std::stoi(argv[3]);
    if (argc >= 5) csv_prefix   = argv[4];

#ifdef _OPENMP
    omp_set_num_threads(num_threads);
    std::cout << "[OMP] Threads configured: " << omp_get_max_threads() << "\n";
#else
    std::cout << "[OMP] OpenMP not available — sequential mode.\n";
#endif

    std::cout << "=======================================================\n"
              << "  SpMV CSR Benchmark  (schedule: static)\n"
              << "=======================================================\n";

    init_mig_counter("./NUMA_scheduler.so");
    CsrMatrix A;
    if (mtx_file.empty()) {
        constexpr IndexType ROWS    = 10000000;
        constexpr IndexType COLS    = 10000000;
        constexpr int       AVG_NNZ = 32;
        std::cout << "\n[INFO] No .mtx file provided → generating random matrix "
                  << ROWS << " x " << COLS
                  << "  avg_nnz_per_row=" << AVG_NNZ << "\n";
        unsigned run_seed = static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count() ^
            (unsigned)std::random_device{}()
        );
        std::cout << "[RNG] Run seed: " << run_seed << "\n";
        A = generate_random_matrix(ROWS, COLS, AVG_NNZ, run_seed);
    } else {
        std::cout << "\n[INFO] Loading file: " << mtx_file << "\n";
        A = load_matrix_market(mtx_file);
    }

    std::cout << "\n[MAT] " << A.num_rows << " x " << A.num_cols
              << "  NNZ=" << A.nnz
              << "  source=" << A.source_file << "\n"
              << "[MAT] Density: "
              << std::scientific << std::setprecision(3)
              << (100.0 * static_cast<double>(A.nnz) /
                  (static_cast<double>(A.num_rows) * A.num_cols))
              << " %\n" << std::defaultfloat;

    std::vector<ValueType> x(A.num_cols), y(A.num_rows);
    init_vector(x);
    init_vector(y, 0.0);

    std::cout << "\n[INFO] Validation with spmv_static...\n";
    spmv_static(A, x, y);
    validate_result(A, x, y);

    std::vector<BenchmarkResult> results;

    init_vector(y, 0.0);
    results.push_back(
        benchmark_spmv(A, x, y, "Static", spmv_static, reps));

    std::cout << "\n"
              << "+----------------------+----------+----------+----------+----------+\n"
              << "| Strategy             | min (ms) | avg (ms) | GFlops   | GiB/s    |\n"
              << "+----------------------+----------+----------+----------+----------+\n";
    for (const auto& r : results) {
        printf("| %-20s | %8.3f | %8.3f | %8.3f | %8.3f |\n",
               r.strategy,
               r.min_time_s * 1e3,
               r.avg_time_s * 1e3,
               r.gflops,
               r.bandwidth_gibs);
    }
    std::cout << "+----------------------+----------+----------+----------+----------+\n";

    std::cout << "\n=== First entries of y = A*x ===\n";
    const int print_n = std::min(10, A.num_rows);
    for (int i = 0; i < print_n; ++i)
        printf("  y[%4d] = %.6f\n", i, y[i]);

    if (!csv_prefix.empty())
        export_csv(csv_prefix + ".csv", A, results);

    return 0;
}