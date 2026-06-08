# Results

Run:

```bash
make run
```

The simulator writes a row for each Figure 9 model/platform point. The key quality target is:

```text
max absolute relative error <= 15%
mean absolute relative error <= 9%
```

These thresholds are enforced by `tests/test_simulator.c`.

Current checked output:

| Metric | Value |
|---|---:|
| Rows | 21 |
| Mean absolute relative error | 8.341% |
| Max absolute relative error | 14.618% |
| Mean relative error | -0.812% |
| Worst case | LLaMA2-70B on Cambricon-LLM-L |

The report in `results/report.md` includes:

- paper decode speed;
- simulated decode speed;
- relative error;
- TPOT;
- derived tile shape;
- read-compute workload fraction;
- aggregate controller command count.

The CSV in `results/figure9_reproduction.csv` additionally includes the no-read-slicing and no-tiling ablation outputs from the same model path.

Additional controller artifacts:

- `results/request_trace.csv`: per-row aggregate `READ_COMPUTE` and `READ_SLICE` requests.
- `results/controller_timing_summary.csv`: per-row controller path balance and command counts.
- `results/npu_timing.csv`: per-row NPU/DRAM timing and TPOT reconstruction.
- `results/latency_breakdown.csv`: operator-group latency mapping for flash weight GeMV, sliced transfer, attention memory, attention compute, and total TPOT.
- `results/controller_schedule.csv`: sample channel/chip/die/plane schedule for OPT-6.7B on Cambricon-LLM-S.
- `results/cycle_controller_trace.csv`: cycle-stepped command trace for the first configured platform.
- `results/cycle_controller_stats.csv`: cycle-level command and resource statistics for that trace.
- `results/ssdsim_ifc_trace.csv`: SSDsim-derived command-stage trace for extended IFC commands.
- `results/ssdsim_ifc_stats.csv`: summary statistics for the SSDsim-derived backend.
- `results/ablation_summary.csv`: no-read-slicing and no-tiling comparisons for Figure 12/Figure 14 style checks.
- `results/figure12_read_slice_ablation.csv`: Cambricon-LLM-S read-slicing ablation against the paper's reported 1.6x-1.8x range.
- `results/figure14_tiling_ablation.csv`: Cambricon-LLM-S hardware-aware tiling ablation against the paper's reported 1.3x-1.4x range.

Additional summary and validation artifacts:

- `results/platform_summary.csv`: per-platform throughput, error, ablation speedup, and command-count summary.
- `results/model_summary.csv`: per-model throughput and error summary across S/M/L platforms.
- `results/tile_profile.csv`: derived tile dimensions, payload, request timing, and read-compute channel occupancy.
- `results/system_profile.csv`: effective context length, NPU frequency/throughput, and DRAM bandwidth.
- `results/reproduction_checks.csv`: pass/fail checks for row count, error bounds, tile size, ablation ranges, and controller balance.

SVG comparison plots:

- `results/figures/figure9_decode_speed.svg`: side-by-side paper/simulator bars for all 21 Figure 9 points.
- `results/figures/figure9_relative_error.svg`: signed relative-error bars with +/-15% reproduction bounds.
- `results/figures/platform_error_summary.svg`: per-platform mean and max absolute error summary.
- `results/figures/controller_schedule_timeline.svg`: Cambricon-LLM-S sample controller timeline showing read-compute windows and sliced reads.
- `results/figures/figure12_read_slice_ablation.svg`: full simulator versus no-read-slicing ablation on Cambricon-LLM-S.
- `results/figures/figure14_tiling_ablation.svg`: full simulator versus no-hardware-aware-tiling ablation on Cambricon-LLM-S.

Current pass/fail checks:

| Check | Value | Target | Status |
|---|---:|---:|---|
| Figure 9 rows | 21 | 21 | PASS |
| Mean absolute relative error | 8.341% | <=9% | PASS |
| Max absolute relative error | 14.618% | <=15% | PASS |
| Cambricon-LLM-S tile height | 256 | 256 | PASS |
| Cambricon-LLM-S tile width | 2048 | 2048 | PASS |
| Read-slicing speedup range | 1.683x-1.699x | 1.6x-1.8x | PASS |
| Tiling speedup range | 1.341x-1.349x | 1.3x-1.4x | PASS |
| Controller path balance delta | 0.000000% | <=1e-6 | PASS |
| Cycle controller trace enabled | 1 | 1 | PASS |
| SSDsim-derived IFC backend enabled | 1 | 1 | PASS |
