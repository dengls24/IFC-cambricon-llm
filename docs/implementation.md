# Implementation Notes

This project is organized as a small C simulator rather than a single-file numerical script.

## C Modules

| File | Responsibility |
|---|---|
| `src/main.c` | Parses `--output-dir`, runs the reproduction, and prints artifact paths. |
| `src/profiles.c` | Holds model profiles, Cambricon-LLM-S/M/L platform profiles, Figure 9 reference points, and opcode names. |
| `src/simulator.c` | Implements the tile-size equations, per-token timing model, NPU timing terms, CSV/JSON/Markdown output writers, and ablation tables. |
| `src/analysis.c` | Writes platform/model summaries, tile profiles, and pass/fail reproduction checks. |
| `src/controller.c` | Implements the SSDsim-style channel/chip/die/plane busy timeline and emits a sample schedule with `READ_COMPUTE` and `READ_SLICE`. |
| `src/plots.c` | Writes standalone SVG plots for Figure 9 reproduction and Figure 12/Figure 14 style ablations. |
| `tests/test_simulator.c` | Checks tile dimensions, opcode naming, Figure 9 error thresholds, controller command accounting, ablation speedup bounds, and nonempty output artifacts. |

## Artifact Mapping

| Artifact | Purpose |
|---|---|
| `results/figure9_reproduction.csv` | Paper-vs-simulator decode-speed table for all 21 Figure 9 points. |
| `results/figures/figure9_decode_speed.svg` | Visual paper-vs-simulator comparison for Figure 9. |
| `results/request_trace.csv` | Aggregate read-compute and sliced-read command counts. |
| `results/controller_schedule.csv` | Concrete channel/chip/die/plane schedule for a representative Cambricon-LLM-S case. |
| `results/controller_timing_summary.csv` | Per-row controller path balance and command totals. |
| `results/npu_timing.csv` | NPU arithmetic, DRAM attention traffic, and reconstructed TPOT. |
| `results/platform_summary.csv` | Per-platform throughput, error, ablation speedup, and command-count summary. |
| `results/model_summary.csv` | Per-model throughput and error summary across platforms. |
| `results/tile_profile.csv` | Derived tile dimensions, request timings, and channel occupancy. |
| `results/reproduction_checks.csv` | Pass/fail reproduction checks used by the result documentation. |
| `results/figures/figure9_relative_error.svg` | Signed Figure 9 error diagnostic with +/-15% bounds. |
| `results/figures/platform_error_summary.svg` | Mean and max absolute error by platform. |
| `results/figures/controller_schedule_timeline.svg` | Controller timeline visualization for a representative Cambricon-LLM-S schedule. |
| `results/figure12_read_slice_ablation.csv` | Read-slicing ablation table. |
| `results/figures/figure12_read_slice_ablation.svg` | Read-slicing ablation plot. |
| `results/figure14_tiling_ablation.csv` | Hardware-aware tiling ablation table. |
| `results/figures/figure14_tiling_ablation.svg` | Hardware-aware tiling ablation plot. |

## Reproduction Scope

The implementation follows the paper's Figure 9 method path:

- flash-resident model weights;
- Section V hardware-aware tile dimensions;
- extended flash-controller commands for tiled read-compute and sliced reads;
- a 16x16, 1 GHz, 2 TOPS INT8 NPU timing path;
- 40 GB/s DRAM timing for attention-cache traffic;
- one platform-level calibration term for command packing and pipeline effects.

The project does not claim to be the authors' original SSDsim fork. It is a reproducible C reconstruction of the timing model and controller behavior needed for the Figure 9 decode-speed comparison plus the Figure 12/Figure 14 style ablation checks.
