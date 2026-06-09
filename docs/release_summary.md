# Release Summary

This document summarizes the release-ready simulator variants, current validation results, and C/SystemC differences.

## Release Scope

The repository is a Cambricon-LLM style in-flash-computing architecture simulator for the Figure 9 decode-speed path. It includes:

- a standalone C timing simulator for the 21 Figure 9 points;
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
- `results/controller_timing_summary.csv`
- `results/npu_timing.csv`
- `docs/figures/performance_results_dashboard.png`
- `docs/figures/performance_results_dashboard.pdf`
- `docs/figures/architecture_summary.png`
- `docs/figures/architecture_summary.pdf`

Current values:

| Metric | Value |
|---|---:|
| Figure 9 rows | 21 |
| Fastest simulated decode speed | 31.115 tokens/s |
| Fastest point | OPT-6.7B on Cambricon-LLM-L |
| LLaMA2-7B on Cambricon-LLM-L | 30.959 tokens/s, 32.301 ms/token |
| LLaMA2-70B on Cambricon-LLM-L | 2.903 tokens/s, 344.473 ms/token |

The Figure 9 reference-fit audit remains in `results/summary.json`: mean absolute relative difference is 8.341%, and the maximum absolute relative difference is 14.618%.

The per-scheme comparison against the Cambricon-LLM paper result is documented in `docs/paper_comparison.md`. The C scheme is the direct 21-point Figure 9 reproduction path. The SystemC component scheme is a representative command-stream cross-check against the C backend anchor and should not be described as an independent 21-point Figure 9 reproduction.

Reference entries for the Cambricon-LLM paper, SSDsim-related simulator background, and SystemC are listed in `docs/references.md` and `data/references.bib`.

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
A standalone C timing simulator with SSDsim-derived IFC event modeling, dependency-free and SystemC cross-checks, and a component-level SystemC command-cycle model for the representative IFC command stream.
```

It should not be described as:

- the original Cambricon-LLM SSDsim fork;
- an RTL implementation;
- a full SSD firmware, FTL, ECC, garbage-collection, or wear model;
- a complete prefill or multi-batch serving simulator.
