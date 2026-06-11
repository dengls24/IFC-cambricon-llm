# Simulator Stack And Version Map

This document separates the released simulator into modeling layers. The goal is to make clear which path owns the paper-facing performance numbers and which paths are validation or audit models.

## Current Release Stack

| Layer | Role | Implementation | Main artifacts | Owns final TPOT? |
|---:|---|---|---|---|
| 0 | Public reproduction profile | `src/profiles.c`, `configs/*.csv` | `results/system_profile.csv`, `results/tile_profile.csv` | No |
| 1 | LLM decode operator trace | `src/simulator.c` | `results/operator_trace.csv`, `results/operator_trace_summary.csv` | Yes |
| 2 | Full-row IFC microcycle weight stage | `src/controller.c` | `results/cycle_weight_timing.csv` | Constrains Layer 1 IFC time |
| 3 | DRAM/NPU attention tail | `src/simulator.c` | `results/npu_timing.csv` | Constrains Layer 1 DRAM/NPU time |
| 4 | SSDsim-derived IFC command backend | `src/ssdsim_ifc.c` | `results/ssdsim_ifc_trace.csv`, `results/ssdsim_ifc_event_trace.csv` | No |
| 5 | C++ and SystemC command-cycle checks | `systemc/*.cpp` | `results/hw_cycle_compare.csv`, `results/systemc_cycle_compare.csv`, `results/systemc_component_compare.csv` | No |
| 6 | Release figures and reports | `results/*.csv`, `docs/figures/*.png`, `docs/*.md` | README figures and release documentation | Reports Layer 1 result |

The paper-facing TPOT is `operator_trace_total_ms`. It is checked as:

```text
operator_trace_total_ms
  = operator_trace_ifc_ms
  + operator_trace_dram_ms
  + operator_trace_npu_ms

operator_trace_ifc_ms  = full-row microcycle-derived IFC weight stage
operator_trace_dram_ms = DRAM attention-cache timing
operator_trace_npu_ms  = NPU attention arithmetic timing
```

## Simulator Variants

| Variant | Command | Meaning | What to cite |
|---|---|---|---|
| Paper-facing C simulator | `make run` | Runs all 21 Cambricon-LLM Figure 9 W8A8 model/platform points. | Token/s, TPOT, latency breakdown, operator trace, ablations. |
| SSDsim-derived C backend | emitted by `make run` | Audits the representative `READ_COMPUTE` and `READ_SLICE` command path using SSDsim-style stages and an event loop. | Command semantics and event-loop evidence, not a full Figure 9 curve. |
| Dependency-free hardware-cycle checker | `make hw-cycle` | Independently checks the representative event stream without `libsystemc`. | Event/completion/cycle agreement with the C event backend. |
| SystemC replay checker | `make systemc-cycle` | Replays the same event rules through the SystemC kernel. | 0-cycle replay equivalence; not a higher-fidelity throughput model. |
| SystemC component model | `make systemc-component` | Splits the representative command stream into controller, execution fabric, FIFO, ONFI bus, plane-array, and IFC-compute modules. | Bounded component timing drift against the C backend. |

## Release Lineage

| Release step | Added capability | Numerical Figure 9 impact |
|---|---|---|
| Full-row IFC cycle timing | Physical command expansion and microcycle-derived flash weight stage. | Established 21-point reproduction guardrails. |
| Microcycle timing refinement | Stage issue-width, FIFO depth, and module-clock quantization fields. | Small TPOT adjustment while preserving guardrails. |
| Operator-trace release | 13-op/layer LLM decode trace; trace becomes top-level TPOT source. | No retuning; trace total equals prior TPOT by construction and test. |
| Visible-figure refresh | PNG/PDF figures redrawn as filled tables and large annotated panels. | No numerical change. |

## What The Architecture Diagram Should Show

The architecture figure in `docs/figures/architecture_summary.png` follows this contract:

1. Inputs and configurable profiles are shown on the left.
2. The paper-facing C path is the top lane and owns the released token/s result.
3. The full-row IFC microcycle scheduler feeds only the IFC service budget of the operator trace.
4. DRAM and NPU timing feed the remaining operator-trace budgets.
5. SSDsim-derived, hardware-cycle, and SystemC models are validation lanes for representative command streams.
6. The output lane separates paper-facing results from validation artifacts.

This is the intended citation boundary: the repository is an auditable public-method architecture simulator, not the original private SSDsim fork, not RTL, and not a complete SSD firmware model.
