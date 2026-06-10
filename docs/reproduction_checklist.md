# Reproduction Checklist

This checklist records what is covered by the current C reproduction and where each result is emitted.

## Method Coverage

| Item | Artifact | Status |
|---|---|---|
| Table II S/M/L flash platform dimensions | `results/tile_profile.csv` | PASS |
| Section V tile shape derivation | `results/tile_profile.csv` | PASS |
| 16x16 1 GHz INT8 NPU timing path | `results/npu_timing.csv` | PASS |
| Effective system profile emission | `results/system_profile.csv` | PASS |
| Context-length inverse-fit sweep | `results/context_length_inference.csv` | PASS |
| DRAM attention-cache traffic timing | `results/npu_timing.csv` | PASS |
| Extended `READ_COMPUTE` command accounting | `results/request_trace.csv` | PASS |
| Sliced read command accounting | `results/request_trace.csv` | PASS |
| Full-row microcycle-derived IFC weight-stage timing | `results/cycle_weight_timing.csv` | PASS |
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
| Optional component-level SystemC trace | `results/systemc_component_trace.csv` | PASS |
| Optional component-level SystemC cross-check | `results/systemc_component_compare.csv` | PASS |
| Optional component-level SystemC VCD | `results/systemc_component.vcd` | PASS |
| Figure 9 decode-speed reproduction | `results/figure9_reproduction.csv` | PASS |
| Publication-facing performance dashboard | `docs/figures/performance_results_dashboard.png` | PASS |
| Publication-facing decode latency breakdown | `docs/figures/decode_latency_breakdown.png` | PASS |
| Publication-facing paper-reference comparison | `docs/figures/paper_reference_comparison.png` | PASS |
| Publication-facing context-length inference | `docs/figures/context_length_inference.png` | PASS |
| Publication-facing SystemC component comparison | `docs/figures/systemc_component_comparison.png` | PASS |
| Publication-facing architecture summary | `docs/figures/architecture_summary.png` | PASS |
| Read-slicing ablation check | `results/figure12_read_slice_ablation.csv` | PASS |
| Hardware-aware tiling ablation check | `results/figure14_tiling_ablation.csv` | PASS |

## Numerical Checks

| Check | Current Value | Target | Status |
|---|---:|---:|---|
| Figure 9 row count | 21 | 21 | PASS |
| Mean absolute relative error | 8.356% | <=9% | PASS |
| Max absolute relative error | 14.508% | <=15% | PASS |
| Inferred context guardrail window | 975-1038 tokens | includes 1000 | PASS |
| Best maximum-error context fit | 1005 tokens | near 1K | PASS |
| Best RMSE context fit | 1030 tokens | near 1K | PASS |
| Full-row microcycle timing rows | 21 | 21 | PASS |
| Maximum full-row microcycle commands | 1,544,720 | >0 | PASS |
| Maximum full-row stage issue events | 3,463,434 | >0 | PASS |
| Maximum full-row dispatch rounds | 1,062,163 | >0 | PASS |
| Cambricon-LLM-S tile height | 256 | 256 | PASS |
| Cambricon-LLM-S tile width | 2048 | 2048 | PASS |
| Cambricon-LLM-S read-slicing speedup | 1.724x-1.741x | 1.6x-1.8x | PASS |
| Cambricon-LLM-S tiling speedup | 1.341x-1.349x | 1.3x-1.4x | PASS |
| Controller path balance delta | 0.000000% | <=1e-6 | PASS |
| Cycle controller trace enabled | 1 | 1 | PASS |
| SSDsim-derived IFC backend enabled | 1 | 1 | PASS |
| SSDsim-derived IFC event loop enabled | 1 | 1 | PASS |
| Hardware-cycle cross-check | PASS | PASS | PASS |
| Component-level SystemC cross-check | PASS | PASS | PASS |

## Build Checks

Run:

```bash
make run
make test
```

The test binary validates formula bounds and writes a temporary artifact set under `/tmp/ifc_cambricon_llm_test_outputs` to verify that primary CSV and local plot-source CSV outputs are produced.

The same test binary also loads the example CSV configuration files and verifies that a custom hardware/model/system run changes throughput and emits nonempty configured artifacts.
