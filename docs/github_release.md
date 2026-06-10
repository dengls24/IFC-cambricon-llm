# GitHub Release Notes

## Suggested Tag

`v0.3-ifc-cambricon-llm-operator-trace`

## Suggested Title

IFC Cambricon-LLM operator-trace C simulator with SSDsim-derived backend and SystemC validation

## Summary

This release publishes the IFC Cambricon-LLM simulator as a reproducible architecture-simulation artifact for the public Cambricon-LLM Figure 9 W8A8 decode-speed path. The paper-facing results are produced by an operator-trace-driven standalone C timing simulator. The released TPOT is sourced from a per-layer LLM decode trace whose flash-side service budget is constrained by a full-row microcycle IFC scheduler, and whose extended IFC command path is audited with an SSDsim-derived C event backend plus C++/SystemC cross-checks.

The simulator is intended to reconstruct the public timing method and expose the modeling assumptions. It is not the authors' private SSDsim fork, not RTL, and not a full SSD firmware/FTL model.

## Highlights

- Reproduces all 21 public Figure 9 W8A8 model/platform points.
- Reports absolute decode throughput and TPOT for OPT and LLaMA2 on Cambricon-LLM-S/M/L.
- Adds a 13-operator-per-layer LLM decode trace with 13,104 default events, 21 row summaries, and zero trace-vs-TPOT delta.
- Infers a stable Figure 9 context-fit window of 975-1038 tokens, with the default reproduction setting at 1000 tokens.
- Uses full-row microcycle-derived IFC weight-stage timing for every Figure 9 row, with up to 1,544,720 physical IFC commands and 3,463,434 stage issues in the largest row.
- Adds an SSDsim-derived C backend for extended `READ_COMPUTE` and `READ_SLICE` command stages.
- Adds SystemC replay equivalence and a component-level SystemC command-cycle model with controller, execution fabric, finite FIFO, issue-width limit, module-clock quantization, and VCD output.
- Keeps Figure 12 read-slicing and Figure 14 hardware-aware tiling ablation checks.

## Current Validation Snapshot

| Check | Result |
|---|---:|
| Figure 9 rows | 21 |
| Mean absolute relative error vs public Figure 9 | 8.356% |
| Max absolute relative error vs public Figure 9 | 14.508% |
| Fastest simulated decode speed | 31.105 tokens/s |
| Fastest simulated point | OPT-6.7B on Cambricon-LLM-L |
| LLaMA2-7B on Cambricon-LLM-L | 30.866 tokens/s, 32.399 ms/token |
| LLaMA2-70B on Cambricon-LLM-L | 2.913 tokens/s, 343.320 ms/token |
| Inferred context window | 975-1038 tokens |
| Default reproduction context | 1000 tokens |
| Largest full-row IFC command schedule | 1,544,720 physical commands |
| Largest full-row stage issue schedule | 3,463,434 stage issues |
| LLM decode operator trace | 13,104 events, 21 row summaries |
| Max operators in one row | 1,040 |
| Max trace-vs-TPOT delta | 0.000000% |
| SSDsim-derived representative command stream | 256 commands, 1536 events |
| SystemC replay delta vs C event backend | 0 cycles |
| SystemC component final-time delta vs C event backend | +85.500 ns, 0.027039% |

## Main Commands

```bash
make run
make test
make test-all
```

If SystemC is not installed system-wide, install a local user-space copy first:

```bash
tools/setup_systemc_local.sh
make test-all
```

The default SystemC sysroot is:

```bash
SYSTEMC_HOME=../.ifc_systemc/systemc_sysroot/usr
```

## Key Outputs

```text
results/summary.json
results/figure9_reproduction.csv
results/latency_breakdown.csv
results/operator_trace.csv
results/operator_trace_summary.csv
results/cycle_weight_timing.csv
results/controller_timing_summary.csv
results/npu_timing.csv
results/context_length_inference.csv
results/figure12_read_slice_ablation.csv
results/figure14_tiling_ablation.csv
results/ssdsim_ifc_trace.csv
results/ssdsim_ifc_event_trace.csv
results/systemc_cycle_compare.csv
results/systemc_component_compare.csv
results/systemc_component.vcd
docs/figures/performance_results_dashboard.png
docs/figures/decode_latency_breakdown.png
docs/figures/operator_trace_breakdown.png
docs/figures/paper_reference_comparison.png
docs/figures/context_length_inference.png
docs/figures/systemc_component_comparison.png
```

## Result Ownership

The standalone C simulator is the direct 21-point Figure 9 reproduction path and owns the released token/s, TPOT, operator-trace, latency-breakdown, and ablation tables.

The SystemC replay checker is an equivalence guard for the representative IFC event stream. Exact agreement with the C backend is expected because it intentionally replays the same command/stage/resource rules through the SystemC kernel.

The component-level SystemC model is the hardware-structured validation path. Its small non-zero final-time drift is expected and comes from finite FIFO boundaries, issue-width limiting, 2.5 ns module-clock quantization, and SystemC process ordering.

## Recommended Method Wording

```text
We implement an operator-trace-driven standalone C timing simulator that reconstructs the Cambricon-LLM Figure 9 decode-speed path using public platform/model parameters, Section V tile equations, a 13-operator-per-layer LLM decode trace, a full-row microcycle-derived IFC weight-stage scheduler, an SSDsim-derived IFC command-stage backend and event loop for READ_COMPUTE and READ_SLICE commands, and C++/SystemC cross-checks. The simulator reproduces all 21 public Figure 9 W8A8 points with 8.356% mean absolute relative error and 14.508% maximum absolute relative error. A component-level SystemC command-cycle model validates a representative IFC command stream with exact event/completion counts and a bounded 0.027039% final-time delta versus the C event backend.
```

## Boundary

Do not describe this release as the original Cambricon-LLM simulator, the authors' private SSDsim fork, a full SSD firmware simulator, an FTL/GC/wear/ECC model, an RTL implementation, a power/area signoff model, or a complete prefill/multi-batch serving simulator.

The right claim is narrower and stronger: this is an auditable public-method C architecture simulator for the Cambricon-LLM Figure 9 decode-speed path, with SSDsim-derived IFC command modeling and C/SystemC validation artifacts.

## Documentation

- `README.md`
- `docs/release_summary.md`
- `docs/method.md`
- `docs/latency_model.md`
- `docs/operator_trace.md`
- `docs/controller_cycle_model.md`
- `docs/ssdsim_ifc_backend.md`
- `docs/paper_comparison.md`
- `docs/simulator_reliability.md`
- `systemc/README.md`
