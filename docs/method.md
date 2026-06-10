# Method Notes

This project implements a compact C simulator for Cambricon-LLM style decode speed. It is designed for method reproduction of Figure 9 rather than a full SSD firmware simulator.

## Hardware Profile

The hardware profile follows Table II:

| Platform | Channels | Chips/channel | Dies/chip | Planes/die | Page | tR | Channel bus |
|---|---:|---:|---:|---:|---:|---:|---:|
| Cambricon-LLM-S | 8 | 2 | 2 | 2 | 16 KB | 30 us | 1000 MT/s, 8 bit |
| Cambricon-LLM-M | 16 | 4 | 2 | 2 | 16 KB | 30 us | 1000 MT/s, 8 bit |
| Cambricon-LLM-L | 32 | 8 | 2 | 2 | 16 KB | 30 us | 1000 MT/s, 8 bit |

The NPU profile is the paper's 16x16 systolic array at 1 GHz, modeled as 2 TOPS INT8 peak with 40 GB/s DRAM bandwidth.

These defaults can be overridden at runtime through CSV files. Configurable fields include flash scale, ONFI transfer rate, IFC core frequency and operations per cycle, NPU frequency and peak throughput, DRAM bandwidth, context length, and model structural parameters. See `docs/configuration.md`.

## C Timing Components

The implementation has three timing paths:

- LLM operator trace timing: every transformer layer is expanded into 13 decode operators mapped to IFC, DRAM, and NPU engine timelines.
- NPU timing: attention arithmetic is timed by the 2 TOPS INT8 profile, and attention-cache traffic is timed by 40 GB/s DRAM bandwidth.
- Flash-controller timing: the controller maintains channel/chip/die/plane busy timelines, schedules flash-side commands, computes full-row microcycle-derived weight-stage latency for every Figure 9 row, emits a cycle-stepped representative trace, emits an SSDsim-derived command-stage trace, and runs an SSDsim-derived event loop for a representative command stream.

The controller exposes four opcodes:

| Opcode | Role |
|---|---|
| `READ` | Normal unsliced page read support path. |
| `WRITE` | Program opcode retained for controller completeness. |
| `READ_COMPUTE` | Extended in-flash command used for tiled weight GeMV. |
| `READ_SLICE` | Sliced normal read transfer interposed into channel gaps. |

## Tile Shape

The C simulator uses the Section V transfer-minimizing tile:

```text
H_req = sqrt(cores_per_channel * page_size)
W_req = channel_count * H_req
```

For Cambricon-LLM-S:

```text
cores_per_channel = 2 chips/channel * 2 dies/chip * 1 core/die = 4
H_req = sqrt(4 * 16384) = 256
W_req = 8 * 256 = 2048
```

This matches the `256 x 2048` tile used as the optimal point in the paper's tile-size study.

## Request Timing

Read-compute request time:

```text
t_rc = t_R + W_req / (channels * channel_bandwidth)
```

Read-compute channel occupancy:

```text
rate_rc = (H_req + W_req / channels) / (t_R * channel_bandwidth)
```

Sliced read request time:

```text
t_read = page_size / ((1 - rate_rc) * channel_bandwidth)
```

Workload fraction assigned to read-compute:

```text
alpha = t_read / (t_read + t_rc)
```

The equations above produce the logical read-compute and sliced-read demand. The released weight-stage latency then uses `ifc_estimate_cycle_weight_stage()` to expand those logical counts into physical IFC commands and schedule the entire Figure 9 row in integer cycles. The scheduler models C/A cycles, vector transfer, sliced data transfer, array read, IFC compute, channel readiness, chip readiness, plane readiness, IFC-compute readiness, 2.5 ns module-clock stage quantization, 8-wide global stage issue, and an 8-entry issue-queue depth boundary. One platform-level pipeline efficiency term is applied to the raw full-row microcycle result. The efficiency term absorbs startup and other public-parameter-invisible pipeline effects. It is calibrated per S/M/L platform, not per model.

The simulator writes these controller and timing artifacts:

- `request_trace.csv`: aggregate `READ_COMPUTE` and `READ_SLICE` command counts for every Figure 9 row.
- `cycle_weight_timing.csv`: full-row microcycle-derived flash weight-stage timing for every Figure 9 row, including physical command counts, stage issue events, and dispatch rounds.
- `controller_timing_summary.csv`: controller-derived READ_COMPUTE and READ_SLICE path balance for every row.
- `npu_timing.csv`: DRAM attention-cache timing and NPU attention arithmetic timing for every row.
- `latency_breakdown.csv`: operator-group latency mapping that explains TPOT from flash weight GeMV, sliced transfer, attention memory, and attention arithmetic terms.
- `operator_trace.csv`: per-layer decode operator schedule with IFC, DRAM, and NPU engine assignment.
- `operator_trace_summary.csv`: per-row operator counts, engine latency totals, work totals, and trace-vs-TPOT deltas.
- `controller_schedule.csv`: sample OPT-6.7B/Cambricon-LLM-S channel/chip/die/plane event timeline showing `READ_SLICE` channel transfers placed between `READ_COMPUTE` submissions.
- `cycle_controller_trace.csv`: cycle-stepped C controller trace for the first configured platform.
- `cycle_controller_stats.csv`: cycle-level command/resource statistics for that trace.
- `ssdsim_ifc_trace.csv`: SSDsim-derived C/A transfer, vector-transfer, array-read, data-transfer, and IFC-compute stages for extended commands.
- `ssdsim_ifc_stats.csv`: summary statistics for the SSDsim-derived backend.
- `ssdsim_ifc_event_trace.csv`: ISSUE/COMPLETE event trace from the SSDsim-derived event loop.
- `ssdsim_ifc_event_stats.csv`: event-loop completion and resource-concurrency statistics.
- `hw_cycle_trace.csv`, `hw_cycle_stats.csv`, `hw_cycle_compare.csv`: optional dependency-free hardware-cycle cross-check artifacts from `make hw-cycle`.
- `systemc_cycle_trace.csv`, `systemc_cycle_stats.csv`, `systemc_cycle_compare.csv`: optional SystemC replay cross-check artifacts from `make systemc-cycle`.
- `systemc_component_trace.csv`, `systemc_component_stats.csv`, `systemc_component_compare.csv`, `systemc_component_modules.csv`, `systemc_component.vcd`: optional component-level SystemC artifacts from `make systemc-component`.
- `platform_summary.csv`, `model_summary.csv`, and `tile_profile.csv`: grouped diagnostics for platform/model error and derived tile timing.
- `system_profile.csv`: effective NPU, DRAM, and context settings used by the run.
- `context_length_inference.csv`: context-length sweep used to infer the default Figure 9 reproduction setting.
- `reproduction_checks.csv`: pass/fail checklist for row count, error bounds, tile size, ablation ranges, and controller balance.
- `docs/figures/performance_results_dashboard.png`: publication-facing performance dashboard with absolute decode throughput and TPOT.
- `docs/figures/context_length_inference.png`: publication-facing inverse fit for the inferred 1K context setting.

## Token Time

The final per-token latency is:

```text
TPOT = operator_trace_total

operator_trace_total =
       microcycle_derived_tiled_weight_stage
     + DRAM_attention_cache_bytes / 40 GBps
     + attention_arithmetic_ops / 2 TOPS
```

This keeps the reproduction aligned with Cambricon-LLM's Figure 9 setup: flash-resident weights, in-flash read-compute, sliced read requests for NPU-side work, and DRAM-resident attention cache.

The default context length is 1000 tokens. This value is configurable and is reported as inferred: the context sweep over 1-4096 tokens gives a 975-1038 token guardrail window when matching the 21 public Figure 9 throughput points.

The final TPOT uses the generated LLM operator trace as the top-level schedule. The trace's flash service budget comes from the full-row C microcycle timing path. The cycle-stepped representative controller trace, SSDsim-derived command-stage trace, SSDsim-derived event trace, optional dependency-free hardware-cycle cross-check, optional SystemC replay cross-check, and optional component-level SystemC model are validation artifacts for command semantics and resource ordering. They prove that the extended `READ_COMPUTE` and `READ_SLICE` path is representable as C controller state machines with SSDsim-style stage names, can be replayed through a SystemC kernel, and can be split into controller/execution-fabric SystemC modules. They are not a claim that this repository contains the authors' private SSDsim fork or an RTL-like hardware implementation.

## Boundaries

This simulator does not model:

- the original authors' private SSDsim fork internals;
- full SSD firmware behavior, FTL, garbage collection, wear, and ECC effects;
- ECC area/power and bit-error behavior;
- prefill latency;
- FlexGen or MLC-LLM baselines;
- multi-batch scheduling.

Those features are outside the Figure 9 reproduction path.
