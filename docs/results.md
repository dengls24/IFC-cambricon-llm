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
- `results/controller_schedule.csv`: sample channel/chip/die/plane schedule for OPT-6.7B on Cambricon-LLM-S.
- `results/ablation_summary.csv`: no-read-slicing and no-tiling comparisons for Figure 12/Figure 14 style checks.
- `results/figure12_read_slice_ablation.csv`: Cambricon-LLM-S read-slicing ablation against the paper's reported 1.6x-1.8x range.
- `results/figure14_tiling_ablation.csv`: Cambricon-LLM-S hardware-aware tiling ablation against the paper's reported 1.3x-1.4x range.
