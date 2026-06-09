# Results

Run:

```bash
make run
```

The simulator writes a row for each Figure 9 model/platform point. The main output is absolute decode performance: simulated tokens/s and TPOT latency for each model on Cambricon-LLM-S/M/L.

## Result Ownership

| Variant | Main artifacts | Result meaning |
|---|---|---|
| Standalone C timing simulator plus SSDsim-derived C event backend | `results/figure9_reproduction.csv`, `results/latency_breakdown.csv`, `results/npu_timing.csv` | Owns the released 21-point token/s and TPOT performance numbers. |
| SystemC replay checker | `results/systemc_cycle_compare.csv` | Checks that the representative C event stream replays with 0-cycle final-time delta through the SystemC kernel. |
| SystemC component command-cycle model | `results/systemc_component_compare.csv`, `results/systemc_component_modules.csv`, `results/systemc_component.vcd` | Checks the same representative command stream after SystemC module decomposition; final-time delta is bounded rather than expected to be exactly zero. |

The SystemC paths are validation paths for command-cycle behavior. They should not be read as separate full Figure 9 throughput curves.

Current checked output:

| Metric | Value |
|---|---:|
| Rows | 21 |
| Fastest simulated decode speed | 31.115 tokens/s |
| Fastest point | OPT-6.7B on Cambricon-LLM-L |
| LLaMA2-7B on Cambricon-LLM-L | 30.959 tokens/s, 32.301 ms/token |
| LLaMA2-70B on Cambricon-LLM-L | 2.903 tokens/s, 344.473 ms/token |
| Inferred Figure 9 context window | 970-1040 tokens |
| Default reproduction context | 1000 tokens |

Reference-fit guardrails are enforced by `tests/test_simulator.c`:

```text
max absolute relative error <= 15%
mean absolute relative error <= 9%
```

The current guardrail values are 8.341% mean absolute relative difference and 14.618% maximum absolute relative difference.

The report in `results/report.md` includes:

- paper decode speed;
- simulated decode speed;
- relative error;
- TPOT;
- derived tile shape;
- read-compute workload fraction;
- context-length inference output;

The release-facing per-scheme paper comparison is in `docs/paper_comparison.md` and `results/simulator_scheme_comparison.csv`.
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
- `results/ssdsim_ifc_event_trace.csv`: ISSUE/COMPLETE event-loop trace for extended IFC commands.
- `results/ssdsim_ifc_event_stats.csv`: event-loop event count, completion, and concurrency statistics.
- `results/hw_cycle_trace.csv`: optional hardware-cycle model event trace from `make hw-cycle`.
- `results/hw_cycle_stats.csv`: optional hardware-cycle model statistics.
- `results/hw_cycle_compare.csv`: C backend versus hardware-cycle cross-check.
- `results/systemc_cycle_trace.csv`: optional SystemC replay event trace from `make systemc-cycle`.
- `results/systemc_cycle_stats.csv`: optional SystemC replay statistics.
- `results/systemc_cycle_compare.csv`: C backend versus SystemC replay equivalence check.
- `results/systemc_component_trace.csv`: optional component-level SystemC event trace from `make systemc-component`.
- `results/systemc_component_stats.csv`: optional component-level SystemC statistics.
- `results/systemc_component_compare.csv`: C backend versus component-level SystemC cross-check, including exact count checks and bounded final-time deltas.
- `results/systemc_component_modules.csv`: ONFI-bus, plane-array, and IFC-compute module issue/completion counts.
- `results/systemc_component.vcd`: high-level SystemC signal trace.
- `results/ablation_summary.csv`: no-read-slicing and no-tiling comparisons for Figure 12/Figure 14 style checks.
- `results/figure12_read_slice_ablation.csv`: Cambricon-LLM-S read-slicing ablation against the paper's reported 1.6x-1.8x range.
- `results/figure14_tiling_ablation.csv`: Cambricon-LLM-S hardware-aware tiling ablation against the paper's reported 1.3x-1.4x range.

Additional summary and validation artifacts:

- `results/platform_summary.csv`: per-platform throughput, error, ablation speedup, and command-count summary.
- `results/model_summary.csv`: per-model throughput and error summary across S/M/L platforms.
- `results/tile_profile.csv`: derived tile dimensions, payload, request timing, and read-compute channel occupancy.
- `results/system_profile.csv`: effective context length, NPU frequency/throughput, and DRAM bandwidth.
- `results/context_length_inference.csv`: 1-4096 token context sweep against the 21 public Figure 9 references.
- `results/reproduction_checks.csv`: pass/fail checks for row count, error bounds, tile size, ablation ranges, and controller balance.

Publication-facing figures:

- `docs/figures/performance_results_dashboard.png`: homepage performance dashboard with absolute token/s and TPOT results.
- `docs/figures/performance_results_dashboard.pdf`: PDF version of the performance dashboard.
- `docs/figures/decode_latency_breakdown.png`: decode TPOT operator breakdown across weight stage, attention memory, and attention compute.
- `docs/figures/decode_latency_breakdown.pdf`: PDF version of the decode latency breakdown.
- `docs/figures/paper_reference_comparison.png`: paper Figure 9 reference versus simulator absolute throughput comparison.
- `docs/figures/paper_reference_comparison.pdf`: PDF version of the paper-reference comparison.
- `docs/figures/context_length_inference.png`: inverse context-length fit showing the stable 970-1040 token window.
- `docs/figures/context_length_inference.pdf`: PDF version of the context-length inference figure.
- `docs/figures/systemc_component_comparison.png`: detailed C-vs-SystemC component comparison for the representative command stream.
- `docs/figures/systemc_component_comparison.pdf`: PDF version of the SystemC component comparison.
- `docs/figures/architecture_summary.png`: homepage simulator architecture summary.
- `docs/figures/architecture_summary.pdf`: PDF version of the architecture summary.

Additional raw comparison plots are kept under `results/figures/` for local inspection when plot generation is enabled.

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
| Inferred context guardrail window | 970-1040 tokens | includes 1000 | PASS |
| Controller path balance delta | 0.000000% | <=1e-6 | PASS |
| Cycle controller trace enabled | 1 | 1 | PASS |
| SSDsim-derived IFC backend enabled | 1 | 1 | PASS |
| SSDsim-derived IFC event loop enabled | 1 | 1 | PASS |
| Hardware-cycle cross-check | PASS | PASS | PASS |
