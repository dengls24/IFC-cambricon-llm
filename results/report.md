# Figure 9 Reproduction Report

This report compares the standalone C IFC simulator against the Cambricon-LLM Figure 9 W8A8 decode-speed points. The simulator includes a C NPU timing path, an SSDsim-inspired flash resource timeline, and a cycle-stepped command trace with extended READ_COMPUTE and READ_SLICE commands.

## Summary

- Rows: 21
- Mean absolute relative error: 8.341%
- Max absolute relative error: 14.618%
- Worst case: llama2_70b on cam_llm_l

## Comparison

| Model | Platform | Paper token/s | Sim token/s | Error | TPOT ms | Tile HxW | Alpha | Commands |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| OPT-6.7B | Cambricon-LLM-S | 3.600 | 3.666 | +1.83% | 272.796 | 256x2048 | 0.355 | 37499 |
| OPT-6.7B | Cambricon-LLM-M | 11.000 | 10.047 | -8.66% | 99.532 | 362x5793 | 0.356 | 9366 |
| OPT-6.7B | Cambricon-LLM-L | 36.300 | 31.115 | -14.28% | 32.139 | 512x16384 | 0.357 | 2339 |
| OPT-13B | Cambricon-LLM-S | 1.900 | 1.878 | -1.14% | 532.405 | 256x2048 | 0.355 | 72759 |
| OPT-13B | Cambricon-LLM-M | 4.700 | 5.247 | +11.63% | 190.597 | 362x5793 | 0.356 | 18174 |
| OPT-13B | Cambricon-LLM-L | 14.200 | 16.017 | +12.80% | 62.434 | 512x16384 | 0.357 | 4538 |
| OPT-30B | Cambricon-LLM-S | 0.800 | 0.799 | -0.08% | 1250.980 | 256x2048 | 0.355 | 167904 |
| OPT-30B | Cambricon-LLM-M | 2.500 | 2.308 | -7.68% | 433.282 | 362x5793 | 0.356 | 41940 |
| OPT-30B | Cambricon-LLM-L | 7.600 | 6.731 | -11.43% | 148.562 | 512x16384 | 0.357 | 10472 |
| OPT-66B | Cambricon-LLM-S | 0.400 | 0.350 | -12.39% | 2853.710 | 256x2048 | 0.355 | 369390 |
| OPT-66B | Cambricon-LLM-M | 1.200 | 1.059 | -11.79% | 944.698 | 362x5793 | 0.356 | 92267 |
| OPT-66B | Cambricon-LLM-L | 2.600 | 2.836 | +9.06% | 352.662 | 512x16384 | 0.357 | 23038 |
| LLaMA2-7B | Cambricon-LLM-S | 3.600 | 3.644 | +1.23% | 274.405 | 256x2048 | 0.355 | 37723 |
| LLaMA2-7B | Cambricon-LLM-M | 10.400 | 9.991 | -3.93% | 100.086 | 362x5793 | 0.356 | 9422 |
| LLaMA2-7B | Cambricon-LLM-L | 34.000 | 30.959 | -8.95% | 32.301 | 512x16384 | 0.357 | 2353 |
| LLaMA2-13B | Cambricon-LLM-S | 1.900 | 1.878 | -1.14% | 532.405 | 256x2048 | 0.355 | 72759 |
| LLaMA2-13B | Cambricon-LLM-M | 4.700 | 5.247 | +11.63% | 190.597 | 362x5793 | 0.356 | 18174 |
| LLaMA2-13B | Cambricon-LLM-L | 14.000 | 16.017 | +14.41% | 62.434 | 512x16384 | 0.357 | 4538 |
| LLaMA2-70B | Cambricon-LLM-S | 0.300 | 0.337 | +12.41% | 2965.466 | 256x2048 | 0.355 | 386180 |
| LLaMA2-70B | Cambricon-LLM-M | 1.000 | 1.041 | +4.06% | 960.942 | 362x5793 | 0.356 | 96461 |
| LLaMA2-70B | Cambricon-LLM-L | 3.400 | 2.903 | -14.62% | 344.473 | 512x16384 | 0.357 | 24085 |

## Controller Artifacts

- `request_trace.csv` records aggregate READ_COMPUTE and READ_SLICE command counts for every Figure 9 row.
- `controller_timing_summary.csv` records controller-derived READ_COMPUTE/READ_SLICE timing balance for every row.
- `npu_timing.csv` records DRAM attention-cache traffic and NPU attention arithmetic timing for every row.
- `latency_breakdown.csv` maps each row to operator groups and reconstructs TPOT.
- `controller_schedule.csv` records one OPT-6.7B/Cambricon-LLM-S event-timeline sample with channel/chip/die/plane placement and busy intervals.
- `cycle_controller_trace.csv` records a C cycle-stepped command trace for the first configured platform.
- `cycle_controller_stats.csv` records cycle-level resource statistics for the same command stream.
- `ssdsim_ifc_trace.csv` records SSDsim-derived C/A transfer, vector-transfer, array-read, data-transfer, and IFC-compute stages for extended commands.
- `ssdsim_ifc_stats.csv` records summary statistics for the SSDsim-derived command backend.
- `ssdsim_ifc_event_trace.csv` records ISSUE/COMPLETE events from the SSDsim-derived event loop.
- `ssdsim_ifc_event_stats.csv` records event-loop completion and resource-concurrency statistics.
- `ablation_summary.csv` records no-read-slicing and no-tiling speed comparisons for the Figure 12/Figure 14 style checks.
- `figure12_read_slice_ablation.csv` and `figure14_tiling_ablation.csv` expose Cambricon-LLM-S specific ablation checks against the paper text ranges.
- `platform_summary.csv` and `model_summary.csv` aggregate reproduction error and throughput by platform/model.
- `tile_profile.csv` records derived tile dimensions, request timings, and read-compute channel occupancy.
- `system_profile.csv` records effective context length, NPU throughput, and DRAM bandwidth.
- `context_length_inference.csv` sweeps decode context length and identifies the Figure 9 best-fit window.
- `reproduction_checks.csv` records pass/fail checks for row count, error bounds, tile size, ablation ranges, and controller balance.
- READ_SLICE channel intervals are emitted between READ_COMPUTE submissions to model the paper's sliced read behavior. This artifact is a command-level controller audit, not a claim of line-by-line equivalence with the private SSDsim fork used by the paper authors.

## Publication Figures

- Publication-facing PNG/PDF figures are stored under `docs/figures/` in the repository.
- `performance_results_dashboard.png` and `performance_results_dashboard.pdf` report standalone C throughput/TPOT and SystemC validation deltas.
- `decode_latency_breakdown.png` and `decode_latency_breakdown.pdf` report decode-stage operator latency breakdowns.
- `paper_reference_comparison.png` and `paper_reference_comparison.pdf` compare simulator throughput against paper Figure 9 references.
- `context_length_inference.png` and `context_length_inference.pdf` show the context-length inverse fit against the paper Figure 9 references.
- `systemc_component_comparison.png` and `systemc_component_comparison.pdf` report detailed C-vs-SystemC component timing comparisons.
- `architecture_summary.png` and `architecture_summary.pdf` summarize the simulator architecture and C/SystemC boundary.
- The C plot helper may emit raw local plot files under this output directory's `figures/` folder for test inspection; those files are not release artifacts.

## Sanity Checks

- First configured platform `cam_llm_s` derives a 256x2048 tile.
- Read-compute workload fraction and channel occupancy are emitted for every model/platform row.
- No-read-slicing and no-tiling rows are produced as controlled ablations from the same C model path.
