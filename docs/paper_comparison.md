# Paper Comparison

This document separates the paper-facing comparison for the two simulator schemes in this repository.

## Comparison Scope

| Scheme | Direct paper target | What is compared | Main artifact |
|---|---|---|---|
| C timing plus SSDsim-derived event backend | Yes | 21 Cambricon-LLM Figure 9 decode-speed points | `results/figure9_reproduction.csv` |
| SystemC component command-cycle model | No, command-stream anchor only | Representative IFC command stream versus the C backend used by the paper-facing simulator | `results/systemc_component_compare.csv` |

The SystemC replay checker is an equivalence checker, not a third independent simulator scheme. Its exact agreement with the C backend is expected by construction.

## Scheme 1: C Timing And SSDsim-Derived Event Backend

This is the direct Figure 9 reproduction path. The paper reference values are the `reference_tokens_per_s` values in `results/figure9_reproduction.csv`; the simulator output is `simulated_tokens_per_s`.

| Metric | Value |
|---|---:|
| Figure 9 rows | 21 |
| Mean absolute relative error | 8.341% |
| Mean relative error | -0.812% |
| Max absolute relative error | 14.618% |
| Worst case | LLaMA2-70B on Cambricon-LLM-L |

Per-point comparison:

| Model | Platform | Paper tokens/s | C simulator tokens/s | Relative error |
|---|---|---:|---:|---:|
| OPT-6.7B | Cambricon-LLM-S | 3.600000 | 3.665743 | 1.826% |
| OPT-6.7B | Cambricon-LLM-M | 11.000000 | 10.047004 | -8.664% |
| OPT-6.7B | Cambricon-LLM-L | 36.300000 | 31.114874 | -14.284% |
| OPT-13B | Cambricon-LLM-S | 1.900000 | 1.878268 | -1.144% |
| OPT-13B | Cambricon-LLM-M | 4.700000 | 5.246682 | 11.632% |
| OPT-13B | Cambricon-LLM-L | 14.200000 | 16.017008 | 12.796% |
| OPT-30B | Cambricon-LLM-S | 0.800000 | 0.799373 | -0.078% |
| OPT-30B | Cambricon-LLM-M | 2.500000 | 2.307964 | -7.681% |
| OPT-30B | Cambricon-LLM-L | 7.600000 | 6.731191 | -11.432% |
| OPT-66B | Cambricon-LLM-S | 0.400000 | 0.350421 | -12.395% |
| OPT-66B | Cambricon-LLM-M | 1.200000 | 1.058539 | -11.788% |
| OPT-66B | Cambricon-LLM-L | 2.600000 | 2.835573 | 9.061% |
| LLaMA2-7B | Cambricon-LLM-S | 3.600000 | 3.644249 | 1.229% |
| LLaMA2-7B | Cambricon-LLM-M | 10.400000 | 9.991360 | -3.929% |
| LLaMA2-7B | Cambricon-LLM-L | 34.000000 | 30.958640 | -8.945% |
| LLaMA2-13B | Cambricon-LLM-S | 1.900000 | 1.878268 | -1.144% |
| LLaMA2-13B | Cambricon-LLM-M | 4.700000 | 5.246682 | 11.632% |
| LLaMA2-13B | Cambricon-LLM-L | 14.000000 | 16.017008 | 14.407% |
| LLaMA2-70B | Cambricon-LLM-S | 0.300000 | 0.337215 | 12.405% |
| LLaMA2-70B | Cambricon-LLM-M | 1.000000 | 1.040646 | 4.065% |
| LLaMA2-70B | Cambricon-LLM-L | 3.400000 | 2.902988 | -14.618% |

Interpretation: this scheme is the only path that should be described as directly reproducing the paper's Figure 9 decode-speed result. It is within 8.341% mean absolute relative error over the 21 paper points.

## Scheme 2: SystemC Component Command-Cycle Model

The component-level SystemC model is a more hardware-structured command-cycle model for a representative IFC command stream. It is not a separate 21-point Figure 9 simulator. It is compared against the C event backend because that backend is the paper-facing simulator's command-stream anchor.

Default component parameters:

| Parameter | Value |
|---|---:|
| Module clock | 2.5 ns |
| Issue width | 8 stage issues per dispatch round |
| Issue FIFO depth | 8 entries |

Comparison against the C backend anchor:

| Metric | C backend | SystemC component | Delta | Status |
|---|---:|---:|---:|---|
| Events | 1536 | 1536 | 0 | PASS |
| Completed commands | 256 | 256 | 0 | PASS |
| Last event cycle | 316207 | 316293 | +86 | PASS |
| Final time ns | 316207.000000 | 316292.500000 | +85.500000 | PASS |

The SystemC component final-time delta is 0.027039% relative to the C backend command-stream final time. This non-zero difference is expected from finite FIFO boundaries, dispatch width limiting, the 2.5 ns module clock, and module-clock quantization of stage service time.

Interpretation: the SystemC component model does not provide a new direct paper Figure 9 curve. It provides a cycle-level cross-check that the paper-facing C simulator's IFC command path remains stable under a more explicit SystemC controller/execution-fabric decomposition. Its timing perturbation is much smaller than the Figure 9 reproduction error envelope of the C scheme.

## Release Statement

Use this wording for release:

```text
The C simulator directly reproduces Cambricon-LLM Figure 9 with 8.341% mean absolute relative error over 21 decode-speed points. The SystemC component model is a command-cycle cross-check of the representative IFC command stream; it preserves event and completion counts exactly and introduces a bounded +85.5 ns final-time delta, or 0.027039%, versus the C backend.
```

Do not describe the SystemC component model as an independent full Figure 9 reproduction or as RTL.
