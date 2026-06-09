# Reproduction Checklist

This checklist records what is covered by the current C reproduction and where each result is emitted.

## Method Coverage

| Item | Artifact | Status |
|---|---|---|
| Table II S/M/L flash platform dimensions | `results/tile_profile.csv` | PASS |
| Section V tile shape derivation | `results/tile_profile.csv` | PASS |
| 16x16 1 GHz INT8 NPU timing path | `results/npu_timing.csv` | PASS |
| Effective system profile emission | `results/system_profile.csv` | PASS |
| DRAM attention-cache traffic timing | `results/npu_timing.csv` | PASS |
| Extended `READ_COMPUTE` command accounting | `results/request_trace.csv` | PASS |
| Sliced read command accounting | `results/request_trace.csv` | PASS |
| Channel/chip/die/plane sample schedule | `results/controller_schedule.csv` | PASS |
| Cycle-stepped controller command trace | `results/cycle_controller_trace.csv` | PASS |
| Cycle-level controller resource statistics | `results/cycle_controller_stats.csv` | PASS |
| SSDsim-derived IFC command-stage trace | `results/ssdsim_ifc_trace.csv` | PASS |
| SSDsim-derived IFC backend statistics | `results/ssdsim_ifc_stats.csv` | PASS |
| SSDsim-derived IFC event-loop trace | `results/ssdsim_ifc_event_trace.csv` | PASS |
| SSDsim-derived IFC event-loop statistics | `results/ssdsim_ifc_event_stats.csv` | PASS |
| Optional hardware-cycle model trace | `results/hw_cycle_trace.csv` | PASS |
| Optional hardware-cycle cross-check | `results/hw_cycle_compare.csv` | PASS |
| Optional SystemC replay trace | `results/systemc_cycle_trace.csv` | PASS |
| Optional SystemC replay equivalence check | `results/systemc_cycle_compare.csv` | PASS |
| Figure 9 decode-speed reproduction | `results/figure9_reproduction.csv` | PASS |
| Figure 9 visual paper-vs-simulator comparison | `results/figures/figure9_decode_speed.svg` | PASS |
| Figure 9 signed error diagnostic | `results/figures/figure9_relative_error.svg` | PASS |
| Read-slicing ablation check | `results/figure12_read_slice_ablation.csv` | PASS |
| Hardware-aware tiling ablation check | `results/figure14_tiling_ablation.csv` | PASS |

## Numerical Checks

| Check | Current Value | Target | Status |
|---|---:|---:|---|
| Figure 9 row count | 21 | 21 | PASS |
| Mean absolute relative error | 8.341% | <=9% | PASS |
| Max absolute relative error | 14.618% | <=15% | PASS |
| Cambricon-LLM-S tile height | 256 | 256 | PASS |
| Cambricon-LLM-S tile width | 2048 | 2048 | PASS |
| Cambricon-LLM-S read-slicing speedup | 1.683x-1.699x | 1.6x-1.8x | PASS |
| Cambricon-LLM-S tiling speedup | 1.341x-1.349x | 1.3x-1.4x | PASS |
| Controller path balance delta | 0.000000% | <=1e-6 | PASS |
| Cycle controller trace enabled | 1 | 1 | PASS |
| SSDsim-derived IFC backend enabled | 1 | 1 | PASS |
| SSDsim-derived IFC event loop enabled | 1 | 1 | PASS |
| Hardware-cycle cross-check | PASS | PASS | PASS |

## Build Checks

Run:

```bash
make run
make test
```

The test binary validates formula bounds and writes a temporary artifact set under `/tmp/ifc_cambricon_llm_test_outputs` to verify that primary CSV and SVG outputs are produced.

The same test binary also loads the example CSV configuration files and verifies that a custom hardware/model/system run changes throughput and emits nonempty configured artifacts.
