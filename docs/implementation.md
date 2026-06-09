# Implementation Notes

This project is organized as a small C simulator rather than a single-file numerical script.

## C Modules

| File | Responsibility |
|---|---|
| `src/main.c` | Parses `--output-dir`, runs the reproduction, and prints artifact paths. |
| `src/profiles.c` | Holds model profiles, Cambricon-LLM-S/M/L platform profiles, Figure 9 reference points, and opcode names. |
| `src/config.c` | Loads runtime CSV overrides for model, platform, system, and reference profiles. |
| `src/simulator.c` | Implements the tile-size equations, per-token timing model, NPU timing terms, CSV/JSON/Markdown output writers, and ablation tables. |
| `src/analysis.c` | Writes platform/model summaries, tile profiles, and pass/fail reproduction checks. |
| `src/controller.c` | Implements the SSDsim-inspired channel/chip/die/plane busy timeline, cycle-stepped command trace, and extended `READ_COMPUTE`/`READ_SLICE` opcodes. |
| `src/ssdsim_ifc.c` | Implements an SSDsim-derived command-stage backend and event loop for `READ_COMPUTE` and `READ_SLICE`. |
| `src/plots.c` | Writes standalone local plots for Figure 9 reproduction and Figure 12/Figure 14 style ablations. |
| `systemc/ifc_hw_cycle_model.cpp` | Implements the optional dependency-free C++ hardware-cycle cross-check model. |
| `systemc/ifc_hw_cycle_systemc.cpp` | Implements the optional `libsystemc` replay checker using `sc_module` and `SC_THREAD`. It shares the C++ command/stage/resource rules and checks kernel-time replay equivalence. |
| `systemc/ifc_component_systemc.cpp` | Implements the optional component-level SystemC command-cycle model with separate controller and execution-fabric modules, FIFO communication, timed stage processes, module statistics, and VCD tracing. |
| `tools/setup_systemc_local.sh` | Installs a local SystemC sysroot without root privileges. |
| `tests/test_simulator.c` | Checks tile dimensions, opcode naming, Figure 9 error thresholds, controller command accounting, cycle-trace consistency, SSDsim-derived stage consistency, dependency-free hardware-cycle cross-check output, ablation speedup bounds, and nonempty output artifacts. |

## Artifact Mapping

| Artifact | Purpose |
|---|---|
| `results/figure9_reproduction.csv` | Paper-vs-simulator decode-speed table for all 21 Figure 9 points. |
| `docs/figures/performance_results_dashboard.png` | Publication-facing performance dashboard with absolute token/s and TPOT. |
| `docs/figures/performance_results_dashboard.pdf` | PDF version of the performance dashboard. |
| `docs/figures/decode_latency_breakdown.png` | Publication-facing decode operator latency breakdown. |
| `docs/figures/decode_latency_breakdown.pdf` | PDF version of the decode latency breakdown. |
| `docs/figures/paper_reference_comparison.png` | Publication-facing paper Figure 9 reference comparison. |
| `docs/figures/paper_reference_comparison.pdf` | PDF version of the paper-reference comparison. |
| `docs/figures/context_length_inference.png` | Publication-facing inverse context-length fit against Figure 9. |
| `docs/figures/context_length_inference.pdf` | PDF version of the context-length inference figure. |
| `docs/figures/systemc_component_comparison.png` | Publication-facing C-vs-SystemC component timing comparison. |
| `docs/figures/systemc_component_comparison.pdf` | PDF version of the SystemC component comparison. |
| `docs/figures/architecture_summary.png` | Publication-facing simulator architecture summary. |
| `docs/figures/architecture_summary.pdf` | PDF version of the architecture summary. |
| `results/request_trace.csv` | Aggregate read-compute and sliced-read command counts. |
| `results/controller_schedule.csv` | Concrete channel/chip/die/plane schedule for a representative Cambricon-LLM-S case. |
| `results/cycle_controller_trace.csv` | Cycle-stepped command trace with channel and array stage timing. |
| `results/cycle_controller_stats.csv` | Cycle-level resource and command statistics for the controller trace. |
| `results/ssdsim_ifc_trace.csv` | SSDsim-derived C/A, vector-transfer, array-read, data-transfer, and IFC-compute stage trace. |
| `results/ssdsim_ifc_stats.csv` | Summary statistics for the SSDsim-derived command backend. |
| `results/ssdsim_ifc_event_trace.csv` | ISSUE/COMPLETE event trace from the SSDsim-derived event loop. |
| `results/ssdsim_ifc_event_stats.csv` | Event-loop completion, event-count, and concurrency statistics. |
| `results/hw_cycle_trace.csv` | Optional hardware-cycle model event trace from `make hw-cycle`. |
| `results/hw_cycle_stats.csv` | Optional hardware-cycle model statistics. |
| `results/hw_cycle_compare.csv` | Cross-check between C SSDsim-derived event backend and hardware-cycle model. |
| `results/systemc_cycle_trace.csv` | Optional SystemC replay event trace from `make systemc-cycle`. |
| `results/systemc_cycle_stats.csv` | Optional SystemC replay statistics. |
| `results/systemc_cycle_compare.csv` | Equivalence check between C SSDsim-derived event backend and SystemC replay. |
| `results/systemc_component_trace.csv` | Optional component-level SystemC event trace from `make systemc-component`. |
| `results/systemc_component_stats.csv` | Optional component-level SystemC statistics. |
| `results/systemc_component_compare.csv` | Cross-check between C SSDsim-derived event backend and component-level SystemC model, with exact count checks and bounded final-timing deltas. |
| `results/systemc_component_modules.csv` | ONFI-bus, plane-array, and IFC-compute module issue/completion counts. |
| `results/systemc_component.vcd` | VCD trace for high-level active/completed/event signals. |
| `results/controller_timing_summary.csv` | Per-row controller path balance and command totals. |
| `results/npu_timing.csv` | NPU arithmetic, DRAM attention traffic, and reconstructed TPOT. |
| `results/latency_breakdown.csv` | Operator-group latency mapping used to reconstruct TPOT. |
| `results/platform_summary.csv` | Per-platform throughput, error, ablation speedup, and command-count summary. |
| `results/model_summary.csv` | Per-model throughput and error summary across platforms. |
| `results/tile_profile.csv` | Derived tile dimensions, request timings, and channel occupancy. |
| `results/system_profile.csv` | Effective NPU, DRAM, and context profile used by the run. |
| `results/context_length_inference.csv` | Context-length sweep used to infer the default Figure 9 reproduction setting. |
| `results/reproduction_checks.csv` | Pass/fail reproduction checks used by the result documentation. |
| `results/figure12_read_slice_ablation.csv` | Read-slicing ablation table. |
| `results/figure14_tiling_ablation.csv` | Hardware-aware tiling ablation table. |
| `results/figures/` | Raw local comparison plots emitted by the C plot helper. |

## Reproduction Scope

The implementation follows the paper's Figure 9 method path:

- flash-resident model weights;
- Section V hardware-aware tile dimensions;
- extended flash-controller commands for tiled read-compute and sliced reads;
- command-level cycle trace generation for controller resource ordering;
- SSDsim-derived command-stage trace generation for extended commands;
- SSDsim-derived event-loop execution for extended commands;
- optional dependency-free hardware-cycle, SystemC replay, and component-level SystemC cross-checks for the same event stream;
- a 16x16, 1 GHz, 2 TOPS INT8 NPU timing path;
- 40 GB/s DRAM timing for attention-cache traffic;
- a context-length inverse-fit sweep for the public Figure 9 points;
- one platform-level calibration term for command packing and pipeline effects.

The project does not claim to be the authors' original SSDsim fork. It is a reproducible C reconstruction of the timing model and controller behavior needed for the Figure 9 decode-speed comparison plus the Figure 12/Figure 14 style ablation checks. The cycle trace is a command-level controller audit, not a full SSD firmware or FTL model.

Runtime configuration is documented in `docs/configuration.md`. The latency model and operator grouping are documented in `docs/latency_model.md`. The controller cycle model is documented in `docs/controller_cycle_model.md`. The SSDsim-derived backend is documented in `docs/ssdsim_ifc_backend.md`. The built-in defaults preserve the paper reproduction path; CSV overrides are intended for design-space experiments unless a matching reference CSV is supplied. The default 1K context setting is inferred from the Figure 9 sweep rather than treated as an explicitly stated paper field.

## Reliability Documentation

The simulator's reliability argument is documented in `docs/simulator_reliability.md`. That document explains the modeling level, parameter transparency, calibration discipline, validation evidence, artifact reproducibility, and boundaries that should be stated when presenting the simulator as a paper-grade architecture reproduction artifact.
