# Paper Comparison

This document separates the paper-facing comparison for the two simulator schemes in this repository.

## Comparison Scope

| Scheme | Direct paper target | What is compared | Main artifact |
|---|---|---|---|
| C timing plus SSDsim-derived event backend | Yes | 21 Cambricon-LLM Figure 9 decode-speed points | `results/figure9_reproduction.csv` |
| SystemC component command-cycle model | No, command-stream anchor only | Representative IFC command stream versus the C backend used by the paper-facing simulator | `results/systemc_component_compare.csv` |

The SystemC replay checker is an equivalence checker, not a third independent simulator scheme. Its exact agreement with the C backend is expected by construction.

The published performance tables should therefore be attributed to the C timing simulator. Its flash-weight stage comes from a full-row cycle scheduler for every Figure 9 row. SystemC results should be cited as cross-check evidence for the representative IFC command stream, not as independent throughput predictions.

## Scheme 1: C Timing And SSDsim-Derived Event Backend

This is the direct Figure 9 reproduction path. The paper reference values are the `reference_tokens_per_s` values in `results/figure9_reproduction.csv`; the simulator output is `simulated_tokens_per_s`.

Publication figure: `docs/figures/paper_reference_comparison.png` and `docs/figures/paper_reference_comparison.pdf`.

Context inference figure: `docs/figures/context_length_inference.png` and `docs/figures/context_length_inference.pdf`.

| Metric | Value |
|---|---:|
| Figure 9 rows | 21 |
| Mean absolute relative error | 8.354% |
| Mean relative error | -0.674% |
| Max absolute relative error | 14.541% |
| Worst case | LLaMA2-13B on Cambricon-LLM-L |
| Inferred context guardrail window | 977-1040 tokens |
| Default reproduction context | 1000 tokens |
| Largest full-row IFC command schedule | 1,544,720 physical commands |

Per-point comparison:

| Model | Platform | Paper tokens/s | C simulator tokens/s | Relative error |
|---|---|---:|---:|---:|
| OPT-6.7B | Cambricon-LLM-S | 3.600000 | 3.664621 | 1.795% |
| OPT-6.7B | Cambricon-LLM-M | 11.000000 | 10.076709 | -8.394% |
| OPT-6.7B | Cambricon-LLM-L | 36.300000 | 31.113179 | -14.289% |
| OPT-13B | Cambricon-LLM-S | 1.900000 | 1.877732 | -1.172% |
| OPT-13B | Cambricon-LLM-M | 4.700000 | 5.263514 | 11.990% |
| OPT-13B | Cambricon-LLM-L | 14.200000 | 16.035697 | 12.927% |
| OPT-30B | Cambricon-LLM-S | 0.800000 | 0.799188 | -0.102% |
| OPT-30B | Cambricon-LLM-M | 2.500000 | 2.315939 | -7.362% |
| OPT-30B | Cambricon-LLM-L | 7.600000 | 6.745747 | -11.240% |
| OPT-66B | Cambricon-LLM-S | 0.400000 | 0.350342 | -12.415% |
| OPT-66B | Cambricon-LLM-M | 1.200000 | 1.062302 | -11.475% |
| OPT-66B | Cambricon-LLM-L | 2.600000 | 2.843882 | 9.380% |
| LLaMA2-7B | Cambricon-LLM-S | 3.600000 | 3.643047 | 1.196% |
| LLaMA2-7B | Cambricon-LLM-M | 10.400000 | 10.016988 | -3.683% |
| LLaMA2-7B | Cambricon-LLM-L | 34.000000 | 30.873976 | -9.194% |
| LLaMA2-13B | Cambricon-LLM-S | 1.900000 | 1.877732 | -1.172% |
| LLaMA2-13B | Cambricon-LLM-M | 4.700000 | 5.263514 | 11.990% |
| LLaMA2-13B | Cambricon-LLM-L | 14.000000 | 16.035697 | 14.541% |
| LLaMA2-70B | Cambricon-LLM-S | 0.300000 | 0.337140 | 12.380% |
| LLaMA2-70B | Cambricon-LLM-M | 1.000000 | 1.044370 | 4.437% |
| LLaMA2-70B | Cambricon-LLM-L | 3.400000 | 2.913724 | -14.302% |

Interpretation: this scheme is the only path that should be described as directly reproducing the paper's Figure 9 decode-speed result. It is within 8.354% mean absolute relative error over the 21 paper points. The default 1K context is an inverse-fit setting supported by the Figure 9 sweep, not a field that the paper text explicitly states for Figure 9.

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

Detailed figure: `docs/figures/systemc_component_comparison.png` and `docs/figures/systemc_component_comparison.pdf`.

Stage service duration comparison:

| Stage | C backend cycles | SystemC component cycles | Delta |
|---|---:|---:|---:|
| C/A transfer | 7 | 8 | +1 |
| IFC vector transfer | 256 | 258 | +2 |
| Array read | 30000 | 30000 | 0 |
| IFC compute | 1024 | 1025 | +1 |
| Data transfer | 4096 | 4098 | +2 |

Interpretation: the SystemC component model does not provide a new direct paper Figure 9 curve. It provides a cycle-level cross-check that the paper-facing C simulator's IFC command path remains stable under a more explicit SystemC controller/execution-fabric decomposition. Its timing perturbation is much smaller than the Figure 9 reproduction error envelope of the C scheme.

## Release Statement

Use this wording for release:

```text
The C simulator directly reproduces Cambricon-LLM Figure 9 with full-row cycle-derived IFC weight-stage timing, 8.354% mean absolute relative error over 21 decode-speed points, and an inferred 977-1040 token context-fit window around the default 1K setting. The SystemC component model is a command-cycle cross-check of the representative IFC command stream; it preserves event and completion counts exactly and introduces a bounded +85.5 ns final-time delta, or 0.027039%, versus the C backend.
```

Do not describe the SystemC component model as an independent full Figure 9 reproduction or as RTL.
