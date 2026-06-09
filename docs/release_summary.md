# Release Summary

This document summarizes the release-ready simulator variants, current validation results, and C/SystemC differences.

## Release Scope

The repository is a Cambricon-LLM style in-flash-computing architecture simulator for the Figure 9 decode-speed path. It includes:

- a standalone C timing simulator for the 21 Figure 9 points, including full-row cycle-derived IFC weight-stage timing;
- an SSDsim-derived C event backend for extended `READ_COMPUTE` and `READ_SLICE` commands;
- a dependency-free C++ hardware-cycle checker;
- a SystemC replay equivalence checker;
- a component-level SystemC command-cycle model.

The simulator does not claim to be the authors' private SSDsim fork, a full SSD firmware simulator, or an RTL implementation.

## Main Reproduction Result

The primary Figure 9 result is emitted by:

```bash
make run
```

Main artifacts:

- `results/summary.json`
- `results/figure9_reproduction.csv`
- `results/simulator_scheme_comparison.csv`
- `results/latency_breakdown.csv`
- `results/cycle_weight_timing.csv`
- `results/controller_timing_summary.csv`
- `results/npu_timing.csv`
- `results/context_length_inference.csv`
- `docs/figures/performance_results_dashboard.png`
- `docs/figures/performance_results_dashboard.pdf`
- `docs/figures/decode_latency_breakdown.png`
- `docs/figures/decode_latency_breakdown.pdf`
- `docs/figures/paper_reference_comparison.png`
- `docs/figures/paper_reference_comparison.pdf`
- `docs/figures/context_length_inference.png`
- `docs/figures/context_length_inference.pdf`
- `docs/figures/architecture_summary.png`
- `docs/figures/architecture_summary.pdf`

Current values:

| Metric | Value |
|---|---:|
| Figure 9 rows | 21 |
| Fastest simulated decode speed | 31.113 tokens/s |
| Fastest point | OPT-6.7B on Cambricon-LLM-L |
| LLaMA2-7B on Cambricon-LLM-L | 30.874 tokens/s, 32.390 ms/token |
| LLaMA2-70B on Cambricon-LLM-L | 2.914 tokens/s, 343.203 ms/token |
| Inferred Figure 9 context window | 977-1040 tokens |
| Default reproduction context | 1000 tokens |
| Largest full-row IFC command schedule | 1,544,720 physical commands |

The Figure 9 reference-fit audit remains in `results/summary.json`: mean absolute relative difference is 8.354%, and the maximum absolute relative difference is 14.541%.

Context length is treated as a configurable reproduction parameter. The inverse-fit sweep in `results/context_length_inference.csv` shows that the default 1000-token setting sits inside the 977-1040 token stable window; the best maximum-error and RMSE fits are 1007 and 1032 tokens respectively. This should be described as inferred from the public Figure 9 curve, not as an explicit Figure 9 field in the paper text.

The per-scheme comparison against the Cambricon-LLM paper result is documented in `docs/paper_comparison.md`. The C scheme is the direct 21-point Figure 9 reproduction path. The SystemC component scheme is a representative command-stream cross-check against the C backend anchor and should not be described as an independent 21-point Figure 9 reproduction.

Reference entries for the Cambricon-LLM paper, SSDsim-related simulator background, and SystemC are listed in `docs/references.md` and `data/references.bib`.

## Variant Result Ownership

| Variant | Command | Released result ownership |
|---|---|---|
| Standalone C timing simulator plus SSDsim-derived C event backend | `make run` | Owns the Figure 9 token/s, TPOT, cycle-weight timing, latency-breakdown, and ablation results. |
| SystemC replay checker | `make systemc-cycle` | Reports lightweight equivalence of a representative IFC event stream against the C event backend. |
| SystemC component command-cycle model | `make systemc-component` | Reports detailed bounded timing drift of a componentized SystemC command stream against the C event backend. |

Only the standalone C path is a direct paper-facing 21-point throughput reproduction. In that path, `results/cycle_weight_timing.csv` is the row-level flash-weight timing source, while the SystemC paths are release validation artifacts for the representative command-cycle backend.

## C Backend

The C event backend is implemented in `src/ssdsim_ifc.c` and writes:

- `results/ssdsim_ifc_event_trace.csv`
- `results/ssdsim_ifc_event_stats.csv`

Current representative command-stream statistics:

| Metric | C backend |
|---|---:|
| commands | 256 |
| completed commands | 256 |
| events | 1536 |
| issue events | 768 |
| complete events | 768 |
| max active commands | 16 |
| last event cycle | 316207 |
| last event ns | 316207.000000 |

## SystemC Replay Checker

The SystemC replay checker is implemented in `systemc/ifc_hw_cycle_systemc.cpp` and is run with:

```bash
make systemc-cycle
```

Artifacts:

- `results/systemc_cycle_trace.csv`
- `results/systemc_cycle_stats.csv`
- `results/systemc_cycle_compare.csv`

Difference versus the C backend:

| Metric | C backend | SystemC replay | Delta | Status |
|---|---:|---:|---:|---|
| events | 1536 | 1536 | 0 | PASS |
| completed commands | 256 | 256 | 0 | PASS |
| last event cycle | 316207 | 316207 | 0 | PASS |

The replay trace is byte-for-byte identical to `results/ssdsim_ifc_event_trace.csv`. This is expected because the replay checker intentionally uses the same command/stage/resource rules and only moves time advancement into the SystemC kernel.

## SystemC Component Model

The component-level SystemC model is implemented in `systemc/ifc_component_systemc.cpp` and is run with:

```bash
make systemc-component
```

Artifacts:

- `results/systemc_component_trace.csv`
- `results/systemc_component_stats.csv`
- `results/systemc_component_compare.csv`
- `results/systemc_component_modules.csv`
- `results/systemc_component.vcd`
- `docs/figures/systemc_component_comparison.png`
- `docs/figures/systemc_component_comparison.pdf`

The model separates the command-cycle simulation into:

- `IfcComponentController`: command state, resource reservation, stage issue, completion handling, and trace emission;
- `IfcExecutionFabric`: timed SystemC stage execution with dynamic processes;
- ONFI-bus, plane-array, and IFC-compute module classes;
- FIFO communication between controller and execution fabric;
- VCD signals for active commands, completed commands, and event count.

Default component parameters:

| Parameter | Value |
|---|---:|
| module clock | 2.5 ns |
| issue width | 8 stage issues per dispatch round |
| issue FIFO depth | 8 entries |

Difference versus the C backend:

| Metric | C backend | SystemC component | Delta | Status |
|---|---:|---:|---:|---|
| events | 1536 | 1536 | 0 | PASS |
| completed commands | 256 | 256 | 0 | PASS |
| last event cycle | 316207 | 316293 | +86 | PASS |
| final time ns | 316207.000000 | 316292.500000 | +85.500000 | PASS |

The component final-time delta is `0.027039%` of the C backend final time. This non-zero delta is expected: the component model applies a finite issue FIFO, an eight-stage issue width, a 2.5 ns module clock, and module-clock quantization of ONFI/data/array/compute service time. For example, the final `READ_SLICE` data transfer is 4096 cycles in the C event backend and 4098 rounded cycles in the component SystemC trace.

Stage service durations in the representative command stream:

| Stage | C backend cycles | SystemC component cycles | Delta |
|---|---:|---:|---:|
| C/A transfer | 7 | 8 | +1 |
| IFC vector transfer | 256 | 258 | +2 |
| Array read | 30000 | 30000 | 0 |
| IFC compute | 1024 | 1025 | +1 |
| Data transfer | 4096 | 4098 | +2 |

Additional component checks:

| Metric | Value |
|---|---:|
| fabric issued stages | 768 |
| fabric completed stages | 768 |
| fabric busy violations | 0 |
| controller timing violations | 0 |
| ONFI-bus issued/completed | 512 / 512 |
| plane-array issued/completed | 128 / 128 |
| IFC-compute issued/completed | 128 / 128 |

Trace-level note:

- The SystemC component model preserves the same command/stage count contract as the C backend.
- The full rows are not byte-for-byte identical because module-clock quantization, dispatch width limiting, finite FIFO boundaries, and same-cycle dynamic process ordering alter issue/completion timestamps and active-command countdowns.
- Event counts and completed command counts remain exact. Final timing is intentionally compared with a bounded tolerance rather than exact equality.

## Validation Commands

Use these commands before release:

```bash
make test
make test-all
```

`make test` validates the C simulator and dependency-free hardware-cycle checker. `make test-all` additionally validates both SystemC paths.

SystemC setup without root privileges:

```bash
tools/setup_systemc_local.sh
```

The default build expects:

```bash
SYSTEMC_HOME=../.ifc_systemc/systemc_sysroot/usr
```

## Release Boundary

This release is suitable as an auditable architecture-simulator artifact for the stated Figure 9 reproduction path. It should be described as:

```text
A standalone C timing simulator with full-row IFC cycle-derived weight-stage timing, SSDsim-derived IFC event modeling, dependency-free and SystemC cross-checks, and a component-level SystemC command-cycle model for the representative IFC command stream.
```

It should not be described as:

- the original Cambricon-LLM SSDsim fork;
- an RTL implementation;
- a full SSD firmware, FTL, ECC, garbage-collection, or wear model;
- a complete prefill or multi-batch serving simulator.
