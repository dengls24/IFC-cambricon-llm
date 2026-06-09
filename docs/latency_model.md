# Latency Model

This document explains how per-token decode latency is computed from model operator groups and the configured hardware profile.

## Scope

The simulator models decode latency at an architecture-paper timing level. It does not replay a framework kernel trace. Instead, it groups decode work into the operator classes that matter for the Cambricon-LLM Figure 9 path:

| Operator group | Hardware path | Modeled by |
|---|---|---|
| Weight GeMV for linear layers | Flash-side IFC | flash-resident weight bytes, tile payload, `READ_COMPUTE`, `READ_SLICE` |
| Attention state memory term | NPU/DRAM | bytes from layers, cache heads, head dim, context length |
| Attention score/value arithmetic term | NPU | ops from layers, attention heads, head dim, context length |

The weight GeMV term aggregates the decode-token linear layers through `parameters_billion`. This is intentional: the Figure 9 timing path is dominated by reading and computing flash-resident weights, and the public paper does not expose per-layer operator traces. The model still keeps the attention memory/compute terms separate so GQA/MQA-style `cache_heads` and `attention_heads` changes are visible.

## Step 1: Tile Derivation

For each configured flash platform:

```text
cores_per_channel = chips_per_channel * dies_per_chip * compute_cores_per_die
H_req = sqrt(cores_per_channel * page_bytes)
W_req = channels * H_req
tile_payload_bytes = channels * cores_per_channel * page_bytes
```

The effective external channel bandwidth is derived from ONFI-style settings:

```text
channel_bandwidth_Bps = onfi_rate_MTps * 1e6 * onfi_bus_width_bits / 8
```

The tile-level IFC compute limit is:

```text
ifc_total_cores =
    channels * chips_per_channel * dies_per_chip * compute_cores_per_die

ifc_peak_ops =
    ifc_total_cores * ifc_frequency_hz * ifc_ops_per_core_cycle

ifc_compute_time_s = H_req * W_req / ifc_peak_ops
```

The per-tile read-compute service time is:

```text
vector_transfer_s = W_req / (channels * channel_bandwidth_Bps)
t_read_compute = array_read_s + max(vector_transfer_s, ifc_compute_time_s)
```

## Step 2: Weight Operator Mapping

The decode-token weight work is represented by model weight bytes:

```text
weight_bytes = parameters_billion * 1e9
request_units = weight_bytes / tile_payload_bytes
```

The simulator computes a workload split between flash-side read-compute and sliced transfer:

```text
read_compute_channel_rate =
    (H_req + W_req / channels) / (array_read_s * channel_bandwidth_Bps)

sliced_read_request_s =
    page_bytes / ((1 - read_compute_channel_rate) * channel_bandwidth_Bps)

alpha_read_compute =
    sliced_read_request_s / (sliced_read_request_s + t_read_compute)

read_compute_requests = request_units * alpha_read_compute
npu_read_requests = request_units * (1 - alpha_read_compute)
npu_read_slices = npu_read_requests * IFC_READ_SLICES_PER_REQUEST
```

The split above produces logical demand. The simulator still records the compact path-balance timing:

```text
read_compute_s = read_compute_requests * t_read_compute
sliced_read_s = npu_read_requests * sliced_read_request_s
overlapped_weight_s = max(read_compute_s, sliced_read_s)
```

The released TPOT does not use this balance equation as the final flash-weight latency. It expands the logical demand into physical IFC commands:

```text
command_multiplier =
    chips_per_channel * dies_per_chip

physical_READ_COMPUTE =
    round(read_compute_requests * command_multiplier)

physical_READ_SLICE =
    round(npu_read_slices * command_multiplier)
```

`src/controller.c::ifc_estimate_cycle_weight_stage()` schedules the full Figure 9 row with integer controller cycles. Every command includes explicit C/A cycles. `READ_COMPUTE` commands include vector transfer, array read, and IFC compute stages. `READ_SLICE` commands include sliced data transfer. The scheduler tracks channel, chip, die, plane, and IFC-compute resource readiness, then reports:

- `cycle_weight_last_cycle`;
- physical `READ_COMPUTE` and `READ_SLICE` command counts;
- raw cycle-derived weight-stage latency;
- calibrated cycle-derived weight-stage latency.

Platform-level command packing and pipeline loss are applied after the raw full-row cycle result:

```text
effective_efficiency =
    pipeline_efficiency /
    (1 + footprint_penalty * (parameters_billion / 70)^footprint_penalty_power)

weight_stage_s = cycle_raw_weight_stage_s / effective_efficiency
```

## Step 3: Attention Terms

The model profile contributes two decode attention terms:

```text
attention_cache_bytes =
    layers * 2 * cache_heads * head_dim * context_tokens

attention_ops =
    2 * layers * attention_heads * head_dim * context_tokens
```

System timing:

```text
attention_cache_s = attention_cache_bytes / dram_bandwidth_Bps
attention_compute_s = attention_ops / npu_peak_ops_per_s
```

If `npu_peak_TOPS` is positive in the system CSV, it is used directly. Otherwise:

```text
npu_peak_ops_per_s = npu_frequency_hz * npu_ops_per_cycle
```

## Step 4: TPOT And Throughput

The final per-token latency is:

```text
TPOT =
    weight_stage_s
  + attention_cache_s
  + attention_compute_s

tokens_per_s = 1 / TPOT
```

The simulator writes both final and decomposed values:

- `results/figure9_reproduction.csv`
- `results/npu_timing.csv`
- `results/controller_timing_summary.csv`
- `results/latency_breakdown.csv`
- `results/cycle_weight_timing.csv`
- `results/cycle_controller_trace.csv`
- `results/cycle_controller_stats.csv`
- `results/ssdsim_ifc_trace.csv`
- `results/ssdsim_ifc_stats.csv`
- `results/ssdsim_ifc_event_trace.csv`
- `results/ssdsim_ifc_event_stats.csv`

The first five artifacts are the primary TPOT reconstruction path. `cycle_weight_timing.csv` is the direct source for the flash-weight stage in `figure9_reproduction.csv`. The representative controller and SSDsim-derived trace artifacts are emitted from the same platform parameters and extended command semantics, but they are intentionally compact validation traces rather than million-command dumps for every Figure 9 row.

## Context-Length Inference

The public paper result does not expose a per-Figure 9 context-length field. This simulator therefore treats context length as a configurable reproduction parameter and also emits an inverse-fit sweep:

- sweep range: 1-4096 decode context tokens;
- target: all 21 public Figure 9 W8A8 throughput points;
- metrics: mean absolute relative error, RMSE relative error, and maximum absolute relative error;
- guardrail: mean absolute relative error <= 9% and maximum absolute relative error <= 15%.

The current default profile gives:

| Fit criterion | Context length | Error summary |
|---|---:|---|
| Best mean absolute error | 555 tokens | 8.207% mean absolute error, but 23.765% maximum error |
| Best maximum-error fit | 1007 tokens | 14.413% maximum error |
| Best RMSE fit | 1032 tokens | 9.701% RMSE |
| Default reproduction setting | 1000 tokens | 8.354% mean absolute error, 14.541% maximum error |

The stable guardrail window is 977-1040 tokens. For release reporting, the repository therefore describes the default as an inferred 1K context setting. This wording is intentional: it is a reproduction fit against Figure 9, not an explicit statement from the paper text.

Artifacts:

- `results/context_length_inference.csv`
- `docs/figures/context_length_inference.png`
- `docs/figures/context_length_inference.pdf`

## Latency Breakdown Artifact

`results/latency_breakdown.csv` maps each row to operator groups:

| Row type | Meaning |
|---|---|
| `flash_weight_gemv` | Logical tiled weight GeMV mapped to flash-side `READ_COMPUTE`. |
| `flash_weight_slice_transfer` | Sliced transfer path for the non-read-compute weight fraction. |
| `effective_weight_stage` | Full-row cycle-derived weight-stage latency after platform efficiency. |
| `attention_state_memory` | DRAM memory term for decode attention state access. |
| `attention_score_value_compute` | NPU compute term for decode attention score/value arithmetic. |
| `total_tpot` | Sum used for final token latency. |

This artifact is the easiest way to audit how model parameters and hardware configuration combine into latency.

Publication figure:

- `docs/figures/decode_latency_breakdown.png`
- `docs/figures/decode_latency_breakdown.pdf`

The figure shows the additive TPOT terms and separately shows the raw full-row cycle weight timing before pipeline calibration, so the flash stage is not presented as a simple sum of dataflow formulas.

## Controller Cycle Audit

`results/cycle_controller_trace.csv` is emitted from a C state machine in `src/controller.c`. It records command stages in controller cycles:

```text
arrival_cycle
channel_start_cycle
channel_end_cycle
array_start_cycle
array_end_cycle
complete_cycle
```

`READ_COMPUTE` commands occupy a channel transfer stage and a plane array-read stage. `READ_SLICE` commands occupy only a channel transfer stage. The trace is checked by `make test` for cycle-count consistency. See `docs/controller_cycle_model.md` for the exact controller boundary.

`results/ssdsim_ifc_trace.csv` adds a second audit path with SSDsim-derived stage names:

```text
READ_COMPUTE: C/A transfer, vector transfer, array read, IFC compute
READ_SLICE:   C/A transfer, data transfer
```

This trace is checked by `make test` for stage coverage and state-name consistency. See `docs/ssdsim_ifc_backend.md`.

`results/ssdsim_ifc_event_trace.csv` records the same representative extended command stream through an event loop. Each stage has an `ISSUE` event and a `COMPLETE` event, and the loop advances to the nearest pending event cycle. The direct full-row TPOT source is `results/cycle_weight_timing.csv`; the event trace is the compact command-semantics audit.

`make hw-cycle` builds `systemc/ifc_hw_cycle_model.cpp` and cross-checks the dependency-free hardware-cycle model against `results/ssdsim_ifc_event_stats.csv`. `make systemc-cycle` builds `systemc/ifc_hw_cycle_systemc.cpp` against `libsystemc` and replays the same command/stage/resource rules through a SystemC `SC_THREAD`; matching event count, completed command count, and last event cycle indicate replay equivalence, not higher hardware fidelity. `make systemc-component` builds `systemc/ifc_component_systemc.cpp`, separates the controller and execution fabric into SystemC modules, runs timed stage processes, and emits module statistics plus a VCD trace. Its compare file checks the same aggregate timing contract against the C event backend.

## What Changes With Configuration

Changing the platform CSV affects:

- tile shape and payload;
- ONFI-derived channel bandwidth;
- read-compute request time;
- sliced-read request time;
- number of commands;
- platform efficiency and ablation terms.

Changing the system CSV affects:

- NPU attention compute time;
- DRAM attention memory time;
- context length.

Changing the model CSV affects:

- weight bytes and request count;
- attention memory term through `cache_heads`;
- attention arithmetic term through `attention_heads`;
- footprint-dependent effective efficiency.

The default profile remains the Figure 9 reproduction path. Custom profiles are design-space experiments unless their reference CSV corresponds to measured or trusted results for that configuration.
