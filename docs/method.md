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

The implementation has two timing paths:

- NPU timing: attention arithmetic is timed by the 2 TOPS INT8 profile, and attention-cache traffic is timed by 40 GB/s DRAM bandwidth.
- Flash-controller timing: the controller maintains channel/chip/die/plane busy timelines, schedules flash-side commands, emits a cycle-stepped trace, emits an SSDsim-derived command-stage trace, and runs an SSDsim-derived event loop for a representative command stream.

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

The tiled weight stage uses the max of the read-compute and sliced-read paths, then applies one platform-level pipeline efficiency term. The efficiency term absorbs startup, imperfect command packing, and controller effects that are not visible in the high-level equations. It is calibrated per S/M/L platform, not per model.

The simulator writes these controller and timing artifacts:

- `request_trace.csv`: aggregate `READ_COMPUTE` and `READ_SLICE` command counts for every Figure 9 row.
- `controller_timing_summary.csv`: controller-derived READ_COMPUTE and READ_SLICE path balance for every row.
- `npu_timing.csv`: DRAM attention-cache timing and NPU attention arithmetic timing for every row.
- `latency_breakdown.csv`: operator-group latency mapping that explains TPOT from flash weight GeMV, sliced transfer, attention memory, and attention arithmetic terms.
- `controller_schedule.csv`: sample OPT-6.7B/Cambricon-LLM-S channel/chip/die/plane event timeline showing `READ_SLICE` channel transfers placed between `READ_COMPUTE` submissions.
- `cycle_controller_trace.csv`: cycle-stepped C controller trace for the first configured platform.
- `cycle_controller_stats.csv`: cycle-level command/resource statistics for that trace.
- `ssdsim_ifc_trace.csv`: SSDsim-derived C/A transfer, vector-transfer, array-read, data-transfer, and IFC-compute stages for extended commands.
- `ssdsim_ifc_stats.csv`: summary statistics for the SSDsim-derived backend.
- `ssdsim_ifc_event_trace.csv`: ISSUE/COMPLETE event trace from the SSDsim-derived event loop.
- `ssdsim_ifc_event_stats.csv`: event-loop completion and resource-concurrency statistics.
- `hw_cycle_trace.csv`, `hw_cycle_stats.csv`, `hw_cycle_compare.csv`: optional hardware-cycle cross-check artifacts from `make hw-cycle`.
- `platform_summary.csv`, `model_summary.csv`, and `tile_profile.csv`: grouped diagnostics for platform/model error and derived tile timing.
- `system_profile.csv`: effective NPU, DRAM, and context settings used by the run.
- `reproduction_checks.csv`: pass/fail checklist for row count, error bounds, tile size, ablation ranges, and controller balance.
- `figures/controller_schedule_timeline.svg`: visual schedule check for the sample Cambricon-LLM-S controller trace.

## Token Time

The final per-token latency is:

```text
TPOT = tiled_weight_stage
     + DRAM_attention_cache_bytes / 40 GBps
     + attention_arithmetic_ops / 2 TOPS
```

This keeps the reproduction aligned with Cambricon-LLM's Figure 9 setup: flash-resident weights, in-flash read-compute, sliced read requests for NPU-side work, and DRAM-resident attention cache.

The final TPOT uses the architecture timing model above. The cycle-stepped controller trace, SSDsim-derived command-stage trace, SSDsim-derived event trace, and optional hardware-cycle cross-check are audit artifacts for command semantics and resource ordering. They prove that the extended `READ_COMPUTE` and `READ_SLICE` path is representable as C controller state machines with SSDsim-style stage names and as a separate hardware-cycle model, but they are not a claim that this repository contains the authors' private SSDsim fork.

## Boundaries

This simulator does not model:

- the original authors' private SSDsim fork internals;
- full SSD firmware behavior, FTL, garbage collection, wear, and ECC effects;
- ECC area/power and bit-error behavior;
- prefill latency;
- FlexGen or MLC-LLM baselines;
- multi-batch scheduling.

Those features are outside the Figure 9 reproduction path.
