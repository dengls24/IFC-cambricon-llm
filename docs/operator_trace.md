# LLM Operator Trace Model

This document records the decode-operator trace layer used by the released TPOT path.

## Scope

The simulator emits a deterministic LLM decode trace from the active model profile. It is not a framework runtime trace captured from PyTorch, TensorRT-LLM, or an NPU compiler. The trace is an architecture-level operator schedule whose work totals are constrained by the same public model, flash, NPU, DRAM, and context parameters used for the Cambricon-LLM Figure 9 reproduction.

Each transformer layer expands to 13 decode operators:

| Index | Operator | Engine | Timing source |
|---:|---|---|---|
| 0 | `attention_norm` | NPU | NPU attention arithmetic budget |
| 1 | `qkv_projection` | IFC | full-row IFC microcycle weight-stage budget |
| 2 | `key_cache_read` | DRAM | attention-cache byte budget |
| 3 | `attention_score` | NPU | NPU attention arithmetic budget |
| 4 | `softmax` | NPU | NPU attention arithmetic budget |
| 5 | `value_cache_read` | DRAM | attention-cache byte budget |
| 6 | `attention_value` | NPU | NPU attention arithmetic budget |
| 7 | `out_projection` | IFC | full-row IFC microcycle weight-stage budget |
| 8 | `mlp_norm` | NPU | NPU attention arithmetic budget |
| 9 | `mlp_gate_up` | IFC | full-row IFC microcycle weight-stage budget |
| 10 | `mlp_activation` | NPU | NPU attention arithmetic budget |
| 11 | `mlp_down` | IFC | full-row IFC microcycle weight-stage budget |
| 12 | `residual` | NPU | NPU attention arithmetic budget |

The default run produces 13,104 operator events: 7 model profiles x 3 platforms x model layer count x 13 operators per layer. The largest row has 1,040 operators.

## Scheduling Rule

The C simulator schedules the trace with per-engine ready times and an inter-operator dependency time:

```text
start_ms = max(dependency_ready_ms, engine_ready_ms)
end_ms = start_ms + service_ms
engine_ready_ms = end_ms
dependency_ready_ms = end_ms
```

This preserves decode-token dependency order while still recording the engine that owns each stage. The current default decode trace is serial at the operator dependency level, so the final trace time equals the sum of IFC, DRAM, and NPU service terms. The point of the trace layer is not to invent independent NPU/DRAM overlap that the public paper does not expose; it is to make the model/operator mapping explicit and testable.

## Timing Budgets

The trace uses three checked timing budgets:

| Budget | Source artifact | Default role |
|---|---|---|
| IFC service time | `results/cycle_weight_timing.csv` | full-row microcycle-derived flash weight stage |
| DRAM service time | `results/npu_timing.csv` | attention-cache traffic at the configured DRAM bandwidth |
| NPU service time | `results/npu_timing.csv` | attention score/value arithmetic at configured NPU throughput |

The released TPOT is `operator_trace_total_ms`. For every Figure 9 row:

```text
operator_trace_total_ms
  = operator_trace_ifc_ms
  + operator_trace_dram_ms
  + operator_trace_npu_ms
  = weight_stage_ms
  + attention_cache_ms
  + attention_compute_ms
```

`make test` checks this equality for every default and custom configuration row.

## Artifacts

- `results/operator_trace.csv`: per-event schedule with model, platform, layer, operator index, engine, mapped stage, start/end time, service time, work bytes/ops, cycle ns, service cycles, and DRAM bursts.
- `results/operator_trace_summary.csv`: per-row event counts, engine counts, engine latency totals, work totals, DRAM burst totals, legacy TPOT, and trace-vs-legacy TPOT delta.
- `results/figure9_reproduction.csv`: includes the operator-trace count and latency summary columns beside the final token/s and TPOT.
- `results/reproduction_checks.csv`: checks that all 21 rows emit operator traces and that the trace TPOT delta is zero within the released tolerance.

Default checked values:

| Metric | Value |
|---|---:|
| Operator trace rows | 21 |
| Operator events | 13,104 |
| Max operators in one row | 1,040 |
| Max trace-vs-TPOT delta | 0.000000% |
| Max DRAM bursts in one row | 18,432,000 |

## Boundary

This trace layer improves the simulator from a pure row-level timing equation to an explicit decode-operator schedule. It still is not RTL, not a real NPU compiler trace, and not the authors' private simulator. NPU internal SRAM, systolic-array pipeline bubbles, tensor-layout transforms, host runtime, and prefill/multi-batch scheduling remain outside the release boundary.
