# IFC Cambricon-LLM

Standalone C simulator plus SystemC replay checker for a Cambricon-LLM style in-flash-computing decode-speed reproduction.

The repository focuses on the method used in "Cambricon-LLM: A Chiplet-Based Hybrid Architecture for On-Device Inference of 70B LLM" for Figure 9:

- Table II flash configurations for Cambricon-LLM-S/M/L.
- A 16x16, 1 GHz, 2 TOPS INT8 NPU with 40 GB/s DRAM bandwidth.
- Section V hardware-aware tiling and read-compute/read-request workload split.
- SSDsim-inspired C flash controller state with channel/chip/die/plane busy timelines, a cycle-stepped command trace, an SSDsim-derived IFC command-stage backend, an event-loop trace, an optional SystemC replay cross-check, and an optional component-level SystemC command-cycle model.
- Extended flash opcodes: `READ`, `WRITE`, `READ_COMPUTE`, and `READ_SLICE`.
- Figure 9 W8A8 decode-speed comparison for OPT and LLaMA2 models at 1K context.

It intentionally keeps the scope narrow: model weights are flash-resident, in-flash computing handles the tiled weight stage, and the attention cache remains in DRAM as in the Cambricon-LLM setup.

## Quick Start

```bash
make run
```

Expected output:

```text
passed: Cambricon-LLM Figure 9 C reproduction
rows: 21
mean_abs_relative_error_pct: ...
max_abs_relative_error_pct: ...
```

The command writes:

- `results/figure9_reproduction.csv`
- `results/summary.json`
- `results/report.md`
- `results/request_trace.csv`
- `results/controller_timing_summary.csv`
- `results/npu_timing.csv`
- `results/latency_breakdown.csv`
- `results/controller_schedule.csv`
- `results/cycle_controller_trace.csv`
- `results/cycle_controller_stats.csv`
- `results/ssdsim_ifc_trace.csv`
- `results/ssdsim_ifc_stats.csv`
- `results/ssdsim_ifc_event_trace.csv`
- `results/ssdsim_ifc_event_stats.csv`
- `results/ablation_summary.csv`
- `results/figure12_read_slice_ablation.csv`
- `results/figure14_tiling_ablation.csv`
- `results/platform_summary.csv`
- `results/model_summary.csv`
- `results/tile_profile.csv`
- `results/system_profile.csv`
- `results/reproduction_checks.csv`
- `results/figures/figure9_decode_speed.svg`
- `results/figures/figure9_relative_error.svg`
- `results/figures/platform_error_summary.svg`
- `results/figures/controller_schedule_timeline.svg`
- `results/figures/figure12_read_slice_ablation.svg`
- `results/figures/figure14_tiling_ablation.svg`

## Current Reproduction Quality

The checked simulator uses a compact per-platform calibration, not per-model fitting. Current outputs:

- Rows: 21 Figure 9 points.
- Mean absolute relative error: 8.341%.
- Max absolute relative error: 14.618%.
- Worst case: LLaMA2-70B on Cambricon-LLM-L.

Run tests:

```bash
make test
```

Run all local checks, including the SystemC replay target when `libsystemc` is available:

```bash
make test-all
```

Run the optional hardware-cycle cross-check:

```bash
make hw-cycle
```

This builds the dependency-free C++ hardware-cycle checker in `systemc/ifc_hw_cycle_model.cpp` and writes `results/hw_cycle_trace.csv`, `results/hw_cycle_stats.csv`, and `results/hw_cycle_compare.csv`.

Run the SystemC replay cross-check:

```bash
make systemc-cycle
```

This builds `systemc/ifc_hw_cycle_systemc.cpp` against `libsystemc` and writes `results/systemc_cycle_trace.csv`, `results/systemc_cycle_stats.csv`, and `results/systemc_cycle_compare.csv`. This target is an equivalence checker: it intentionally replays the same command/stage/resource rules through the SystemC kernel, so exact agreement with the C event backend is expected and is not evidence of higher hardware fidelity. On this machine, SystemC is installed locally at `../.ifc_systemc/systemc_sysroot/usr`; override with `SYSTEMC_HOME=/usr` for a system package install.

Run the component-level SystemC model:

```bash
make systemc-component
```

This builds `systemc/ifc_component_systemc.cpp` and writes `results/systemc_component_trace.csv`, `results/systemc_component_stats.csv`, `results/systemc_component_compare.csv`, `results/systemc_component_modules.csv`, and `results/systemc_component.vcd`. The component model separates the controller and execution fabric into SystemC modules and uses timed stage processes for ONFI bus, plane-array, and IFC-compute work. It remains a command-cycle architecture model, not RTL.

For the default command stream, the component model intentionally does not match the C event backend cycle-for-cycle: it reports `last_event_cycle=316293` and `final_time_ns=316292.500000`, versus the C backend's `316207` cycles and `316207.000000 ns`. The delta is `+86` rounded cycles, or `+85.500000 ns` actual SystemC time (`0.027039%`). See [docs/release_summary.md](docs/release_summary.md) for the C/SystemC breakdown.

Run all SystemC validation paths:

```bash
make systemc-full
```

If SystemC is not installed, run:

```bash
tools/setup_systemc_local.sh
```

The script downloads and extracts `libsystemc`/`libsystemc-dev` into `../.ifc_systemc` without root privileges.

## Method

The simulator derives the optimal tile shape from the paper's Section V formulation:

```text
H_req = sqrt(cores_per_channel * page_size)
W_req = channel_count * H_req
```

For Cambricon-LLM-S this gives `256 x 2048`, matching the tile-size study in the paper. The simulator then computes:

- read-compute request time from array read latency and input-vector transfer;
- read-compute channel occupancy;
- sliced read request time from the remaining channel bandwidth;
- workload fraction `alpha = t_read / (t_read + t_read_compute)`;
- overlapped tiled weight-stage time;
- DRAM attention-cache traffic and NPU attention arithmetic.

More detail is in [docs/method.md](docs/method.md). Latency calculation is explained in [docs/latency_model.md](docs/latency_model.md). The cycle-stepped controller trace is documented in [docs/controller_cycle_model.md](docs/controller_cycle_model.md). The SSDsim-derived IFC backend is documented in [docs/ssdsim_ifc_backend.md](docs/ssdsim_ifc_backend.md). The hardware-cycle and SystemC paths are documented in [systemc/README.md](systemc/README.md). Runtime hardware/model configuration is documented in [docs/configuration.md](docs/configuration.md). Results and plot outputs are summarized in [docs/results.md](docs/results.md). Module responsibilities are listed in [docs/implementation.md](docs/implementation.md). The pass/fail reproduction checklist is in [docs/reproduction_checklist.md](docs/reproduction_checklist.md). Simulator reliability and modeling credibility are discussed in [docs/simulator_reliability.md](docs/simulator_reliability.md). Release-ready C/SystemC differences are summarized in [docs/release_summary.md](docs/release_summary.md).

## Configurable Experiments

The default run uses the built-in paper profile. Design-space runs can override flash scale, ONFI bandwidth, IFC frequency/throughput, NPU frequency/throughput, DRAM bandwidth, context length, and model parameters:

```bash
make all
bin/ifc_cambricon_llm \
  --output-dir results_scaled \
  --models-csv configs/example_models_mixed.csv \
  --platforms-csv configs/example_scaled_platforms.csv \
  --system-csv configs/example_system_fast_npu.csv \
  --reference-csv configs/default_references.csv
```

When custom hardware or model profiles are used with default references, token/s values are design-space estimates; relative-error metrics are only reproduction claims when the reference CSV matches the configured setup.

## Repository Layout

```text
data/         Paper references and hardware/model profiles
docs/         Method, implementation, and result notes
include/      Public C header
src/main.c    CLI entry point
src/profiles.c
             Model/platform/reference profile tables
src/config.c  Runtime CSV configuration loader
src/simulator.c
             Tile model, timing model, CSV/JSON/Markdown outputs
src/analysis.c
             Platform/model summaries, tile profile, reproduction checks
src/controller.c
             SSDsim-inspired flash-controller schedule, cycle trace, and extended opcodes
src/ssdsim_ifc.c
             SSDsim-derived IFC command-stage backend
systemc/     Hardware-cycle model, SystemC replay checker, and SystemC component model
tools/       Local SystemC setup helper
src/plots.c   SVG comparison plot writer
tests/        C smoke tests for formulas and reproduction bounds
results/      Reproduction outputs and SVG figures
```
