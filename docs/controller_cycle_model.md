# Controller Cycle Model

This document records the controller modeling level used by the standalone C simulator.

The important boundary is explicit: this repository is not the paper authors' private SSDsim fork and does not claim line-by-line equivalence with that implementation. The current project implements a C command-level, cycle-stepped flash controller model that follows the Cambricon-LLM method path and uses SSDsim-inspired resource concepts: channels, chips, dies, planes, array service time, channel transfer time, and command completion.

## Why This Is Not Only A Dataflow Formula

The main Figure 9 latency path is reconstructed from model operator groups and hardware parameters. That part is described in `docs/latency_model.md`.

The controller side adds one full-row timing artifact and several compact validation traces:

| Artifact | Model level |
|---|---|
| `results/cycle_weight_timing.csv` | Full-row microcycle-derived flash weight-stage timing for every Figure 9 model/platform row. |
| `results/controller_schedule.csv` | Event timeline in ns for a representative command stream. |
| `results/cycle_controller_trace.csv` | Cycle-stepped C controller trace for the same class of command stream. |
| `results/cycle_controller_stats.csv` | Cycle-level aggregate statistics for that trace. |
| `results/ssdsim_ifc_trace.csv` | SSDsim-derived command-stage trace for the extended command stream. |
| `results/ssdsim_ifc_stats.csv` | Summary statistics for the SSDsim-derived backend. |
| `results/ssdsim_ifc_event_trace.csv` | ISSUE/COMPLETE event-loop trace for the extended command stream. |
| `results/ssdsim_ifc_event_stats.csv` | Event-loop completion and concurrency statistics. |

The cycle artifacts are produced by `src/controller.c`, not by post-processing equations. Each representative controller command moves through explicit stages:

```text
QUEUED -> CHANNEL -> WAIT_ARRAY -> ARRAY -> DONE
```

For `READ_SLICE`, the array stage is bypassed:

```text
QUEUED -> CHANNEL -> DONE
```

The test binary parses `cycle_controller_trace.csv` and checks that:

- `channel_end_cycle - channel_start_cycle == channel_cycles`;
- `array_end_cycle - array_start_cycle == array_cycles` for `READ_COMPUTE`;
- `READ_SLICE` has no array stage;
- completion cycles are ordered after channel and array service;
- both `READ_COMPUTE` and `READ_SLICE` appear in the command stream.

## Command Extensions

The controller recognizes four opcodes:

| Opcode | Controller behavior |
|---|---|
| `READ` | Normal read path with channel service and array read service. |
| `WRITE` | Program path with channel service and program service. |
| `READ_COMPUTE` | Extended command for tiled in-flash weight GeMV. It occupies a channel transfer slot, then a plane array-read slot. |
| `READ_SLICE` | Extended sliced transfer command. It occupies channel bandwidth only and is interposed between read-compute submissions. |

The current trace stream uses the extended commands needed for the Cambricon-LLM Figure 9 decode path: `READ_COMPUTE` and `READ_SLICE`.

## Cycle Granularity

The controller cycle time is derived from the configured IFC clock:

```text
cycle_ns = 1e9 / ifc_frequency_hz
```

For each command, service times are rounded up to whole controller cycles:

```text
channel_cycles = ceil(channel_transfer_ns / cycle_ns)
array_cycles   = ceil(array_read_ns / cycle_ns)
```

Channel transfer time is derived from the ONFI-style external bus fields:

```text
channel_bandwidth_Bps = onfi_rate_MTps * 1e6 * onfi_bus_width_bits / 8
```

The default Cambricon-LLM-S profile uses a 1 GHz IFC clock, so the cycle trace has 1 ns cycles.

## Resource Model

The cycle controller keeps these active resources:

- one active command per flash channel;
- one active array command per channel/chip/die/plane;
- FIFO issue order over the generated command stream;
- configurable channel, chip, die, and plane counts up to the internal checked limits.

At every cycle, the controller:

1. issues queued commands to free channel resources;
2. moves channel-complete read-compute commands into the waiting-array stage;
3. issues waiting array commands to free planes;
4. decrements active channel and array service counters;
5. records end cycles and completion cycles.

This is a command-level cycle model. It is stronger than a pure dataflow equation because resource conflicts and completion order come from a C state machine. It is still not RTL or a complete SSD firmware simulator.

## Relation To Figure 9 Latency

The final TPOT in `figure9_reproduction.csv` is computed from:

```text
TPOT = operator_trace_total

operator_trace_total =
       effective_weight_stage
     + attention_state_memory
     + attention_score_value_compute
```

The top-level schedule is emitted as `results/operator_trace.csv` and summarized in `results/operator_trace_summary.csv`. The weight stage inside that trace is derived in two steps. First, the paper's Section V tile/request equations produce logical read-compute and sliced-read demand from the configured flash platform and model size. Second, `ifc_estimate_cycle_weight_stage()` expands that demand into physical channel/chip/die commands and schedules the whole Figure 9 row with integer controller cycles plus microarchitectural issue constraints.

For each row, the full-row scheduler records:

- physical `READ_COMPUTE` and `READ_SLICE` command counts;
- final row completion cycle;
- C/A, vector-transfer, data-transfer, array-read, and IFC-compute cycle costs;
- 2.5 ns module-clock quantized stage durations;
- 8-wide stage issue, 8-entry issue-queue depth, stage issue events, and dispatch rounds;
- raw microcycle-derived weight-stage time;
- platform-efficiency calibrated weight-stage time used by TPOT.

Those values are written to `results/cycle_weight_timing.csv` and copied into the cycle columns in `results/figure9_reproduction.csv`. A literal trace file for every physical command is deliberately not emitted because the largest default row schedules 1,544,720 physical commands and 3,463,434 stage issue events. The compact trace files remain representative validation artifacts for command semantics and resource ordering.

This separation is intentional:

- the Figure 9 table uses the full-row microcycle-derived weight stage plus NPU/DRAM attention timing needed for the paper reproduction;
- the operator trace maps those row-level budgets onto per-layer decode operators and owns the released TPOT field;
- `cycle_weight_timing.csv` proves that each Figure 9 row has been expanded to physical IFC commands and scheduled through integer cycles with finite stage issue bandwidth;
- the representative controller trace proves that the extended command stream is representable as a C cycle-level resource schedule;
- the SSDsim-derived trace proves that the same extended commands can be represented as C/A, array-read, data-transfer, and IFC-compute service stages;
- the SSDsim-derived event trace proves that those service stages can execute through a next-event loop with explicit ISSUE and COMPLETE events;
- `docs/latency_model.md` links the model operators to the final latency terms.

## What A Full SSDsim Fork Would Add

A complete SSDsim fork integration would require these additional pieces inside the original SSDsim event loop:

- new SSDsim request/command enum values for `READ_COMPUTE` and `READ_SLICE`;
- parser support for the new commands in trace or online request input;
- command handlers attached to channel, chip, die, and plane state machines;
- timing hooks for array read latency, program latency, channel transfer time, and controller overhead;
- full interaction with SSDsim FTL, mapping, queueing, garbage collection, and statistics;
- validation that the extended commands preserve existing SSDsim behavior for normal read and write requests.

Those pieces are not claimed in this standalone repository. The current implementation is a clean C reconstruction of the public Cambricon-LLM method path with a full-row IFC cycle timing path and an SSDsim-inspired command/resource validation model.

## How To Audit

Run:

```bash
make run
make test
```

Then inspect:

- `results/cycle_controller_trace.csv`
- `results/cycle_controller_stats.csv`
- `results/cycle_weight_timing.csv`
- `results/ssdsim_ifc_trace.csv`
- `results/ssdsim_ifc_stats.csv`
- `results/ssdsim_ifc_event_trace.csv`
- `results/ssdsim_ifc_event_stats.csv`
- `results/controller_schedule.csv`
- `results/operator_trace.csv`
- `results/operator_trace_summary.csv`
- `results/latency_breakdown.csv`

The tests validate both numerical reproduction bounds and cycle-trace consistency.
